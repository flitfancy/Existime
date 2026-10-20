"""Bounded CPU worker for AEC, VAD and processed recording; raw transport stays independent."""
from __future__ import annotations

import queue
import threading
import time
from importlib.metadata import distribution
from pathlib import Path

import numpy as np
from scipy.signal import firwin, lfilter


class StereoResampler:
    """Stateful 24 kHz -> 16 kHz FIR converter; channels remain independent."""
    def __init__(self):
        self.taps = (firwin(63, 1 / 3) * 2).astype(np.float32)
        self.reset()

    def reset(self):
        self.history = np.zeros((62, 2), np.float32)
        self.dc = np.zeros((1, 2), np.float32)
        self.phase = 0

    def process(self, samples):
        # Remove microphone DC with continuous filter state, without automatic gain.
        samples, self.dc = lfilter([1., -1.], [1., -.995], samples,
                                   axis=0, zi=self.dc)
        up = np.zeros((len(samples) * 2, 2), np.float32)
        up[::2] = samples
        filtered, self.history = lfilter(self.taps, [1.], up, axis=0, zi=self.history)
        output = filtered[self.phase::3].astype(np.float32)
        self.phase = (self.phase - len(up)) % 3
        return output


class SileroCpu:
    def __init__(self):
        import onnxruntime as ort
        model = Path(distribution('silero-vad').locate_file('silero_vad/data/silero_vad.onnx'))
        options = ort.SessionOptions()
        options.intra_op_num_threads = 1
        options.inter_op_num_threads = 1
        self.session = ort.InferenceSession(str(model), sess_options=options,
                                            providers=['CPUExecutionProvider'])
        self.reset()

    def reset(self):
        self.state = np.zeros((2, 2, 128), np.float32)
        self.context = np.zeros((2, 64), np.float32)

    def probabilities(self, samples):
        if samples.shape != (512, 2):
            raise ValueError('VAD expects two channels of 512 samples at 16 kHz')
        inputs = np.concatenate((self.context, samples.T), axis=1)
        probability, self.state = self.session.run(None, {
            'input': inputs, 'state': self.state, 'sr': np.array(16000, np.int64),
        })
        self.context = inputs[:, -64:].copy()
        return probability[:, 0].tolist()


class SpeechActivity:
    """96 ms speech confirmation and 512 ms end hold; not a wake word detector."""
    def __init__(self):
        self.active = False
        self.positive = self.silent = 0

    def update(self, probability):
        event = ''
        if not self.active:
            self.positive = self.positive + 1 if probability >= .5 else 0
            if self.positive >= 3:
                self.active = True
                self.silent = 0
                event = 'speech-start'
        else:
            self.silent = self.silent + 1 if probability < .35 else 0
            if self.silent >= 16:
                self.active = False
                self.positive = 0
                event = 'speech-end'
        return event


class VadObserver:
    def __init__(self, processor=None, wakeword=None):
        self.processor = processor
        self.wakeword = wakeword
        # Reserve control delivery even when the 320 ms audio budget is exceeded.
        self.queue = queue.Queue()
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = None
        self.generation = 0
        self.data = {
            'engine': 'Silero VAD 6.2.1 · CPU', 'status': 'loading', 'speech': False,
            'probability': 0., 'left_probability': 0., 'right_probability': 0.,
            'segments': 0, 'last_event': '', 'last_update': 0., 'playback_overlap': False,
            'processing_ms_per_32ms': 0., 'cpu_one_core_percent': 0.,
            'queue_drops': 0, 'discontinuities': 0, 'windows': 0, 'error': '',
            'aec_enabled': False, 'mode': 'observation',
        }

    def start(self):
        if not self.thread:
            self.thread = threading.Thread(target=self.run, name='ffv-vad', daemon=True)
            self.thread.start()

    def reset(self):
        if self.wakeword:self.wakeword.invalidate()
        with self.lock:
            self.generation += 1
            self.data.update(status='waiting', speech=False, probability=0.,
                             left_probability=0., right_probability=0., last_update=0.)
        self.drain()

    def drain(self):
        controls = []
        while True:
            try:
                item = self.queue.get_nowait()
                if item[0] == 'finish': controls.append(item)
            except queue.Empty:break
        for item in controls:self.queue.put_nowait(item)

    def finish_capture(self, capture):
        self.queue.put_nowait(('finish', capture))

    def offer(self, sequence, payload, playback, mode, overflow=False, capture=None):
        with self.lock:
            generation = self.generation
        timestamp = time.monotonic()
        context = self.processor.reference.context(timestamp) if self.processor else None
        item = (generation, sequence, payload, bool(playback), mode, bool(overflow), timestamp, capture, context)
        if self.queue.qsize() >= 32:
            with self.lock:self.data['queue_drops'] += 1
            return
        self.queue.put_nowait(item)

    def snapshot(self):
        with self.lock:result = dict(self.data)
        if result['status'] == 'ready' and time.time() - result['last_update'] > 1:
            result.update(status='waiting', speech=False, probability=0.,
                          left_probability=0., right_probability=0.)
        return result

    def run(self):
        try:
            model = SileroCpu()
            # Warm inference before attaching to live packets.
            for _ in range(4):model.probabilities(np.zeros((512, 2), np.float32))
            model.reset()
            converter = StereoResampler()
            activity = SpeechActivity()
            pending = np.empty((0, 2), np.float32)
            last_sequence = generation = mode = None
            wall_total = cpu_total = audio_total = 0.
            windows = 0
            probabilities = [0., 0.]
            with self.lock:self.data['status'] = 'waiting'
            while not self.stop.is_set():
                try:item = self.queue.get(timeout=.2)
                except queue.Empty:continue
                if item[0] == 'finish':
                    try:item[1].finish()
                    except Exception as exc:
                        with self.lock:self.data['error'] = str(exc)[:240]
                    continue
                epoch, seq, payload, playback, selected, overflow, enqueued, capture, context = item
                with self.lock:current_generation = self.generation
                if epoch != current_generation:continue
                discontinuity = (generation != epoch or last_sequence is None or
                    seq != ((last_sequence + 1) & 0xffffffff) or overflow or
                    time.monotonic() - enqueued > .5)
                if discontinuity:
                    if self.wakeword:self.wakeword.invalidate()
                    model.reset();converter.reset();activity = SpeechActivity()
                    probabilities = [0., 0.]
                    if self.processor and generation != epoch:self.processor.reset()
                    pending = np.empty((0, 2), np.float32)
                    with self.lock:self.data['discontinuities'] += 1
                if mode != selected:activity = SpeechActivity()
                mode = selected;generation = epoch;last_sequence = seq
                wall_start = time.perf_counter();cpu_start = time.thread_time()
                samples = np.frombuffer(payload, dtype='<i2').reshape(-1, 2).astype(np.float32) / 32768.
                converted = converter.process(samples)
                processed = self.processor.process(converted, seq, context, overflow) if self.processor else converted
                pending = np.concatenate((pending, processed), axis=0)
                audio_total += len(samples) / 24000
                updates = None
                while len(pending) >= 512:
                    left, right = model.probabilities(pending[:512])
                    probabilities = [left, right]
                    pending = pending[512:]
                    probability = left if selected == 'left' else right if selected == 'right' else max(left, right)
                    event = activity.update(probability);windows += 1
                    updates = dict(status='ready', speech=activity.active, probability=round(probability, 4),
                        left_probability=round(left, 4), right_probability=round(right, 4),
                        last_update=time.time(), playback_overlap=playback, windows=windows)
                    with self.lock:
                        if event:
                            self.data['last_event'] = event
                            if event == 'speech-start':self.data['segments'] += 1
                if capture:
                    capture.process_clean(seq, processed, probabilities, playback)
                if self.wakeword:self.wakeword.offer(seq, processed, selected)
                wall_total += time.perf_counter() - wall_start
                cpu_total += time.thread_time() - cpu_start
                with self.lock:
                    if generation == self.generation:
                        if updates:self.data.update(updates)
                        if self.processor:
                            aec = self.processor.snapshot()
                            self.data.update(aec_enabled=aec['enabled'], mode='recognition', aec_status=aec['status'])
                        if windows:
                            self.data.update(processing_ms_per_32ms=round(wall_total * 1000 / windows, 3),
                                cpu_one_core_percent=round(cpu_total / max(audio_total, .001) * 100, 2))
        except Exception as exc:
            with self.lock:self.data.update(status='error', error=str(exc)[:240], speech=False)
