"""Local keyword spotting on processed PCM, isolated from the audio worker."""
from __future__ import annotations

import json
import os
import queue
import threading
import time
from pathlib import Path

import numpy as np

KEYWORD = '小萤小萤'
DEFAULT_MODEL = Path(__file__).resolve().parent / 'models' / 'sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20'


class KeywordModel:
    def __init__(self):
        import sherpa_onnx
        root = Path(os.environ.get('FFV_WAKE_MODEL', str(DEFAULT_MODEL)))
        for name in ('tokens.txt', 'encoder-epoch-13-avg-2-chunk-16-left-64.int8.onnx',
                     'decoder-epoch-13-avg-2-chunk-16-left-64.onnx',
                     'joiner-epoch-13-avg-2-chunk-16-left-64.int8.onnx'):
            if not (root / name).is_file():raise FileNotFoundError('唤醒模型缺少文件：' + name)
        self.spotter = sherpa_onnx.KeywordSpotter(
            tokens=str(root / 'tokens.txt'),
            encoder=str(root / 'encoder-epoch-13-avg-2-chunk-16-left-64.int8.onnx'),
            decoder=str(root / 'decoder-epoch-13-avg-2-chunk-16-left-64.onnx'),
            joiner=str(root / 'joiner-epoch-13-avg-2-chunk-16-left-64.int8.onnx'),
            keywords_file=str(Path(__file__).with_name('wakeword-keywords.txt')),
            num_threads=1, provider='cpu')
        self.reset()

    def reset(self):
        self.streams = [self.spotter.create_stream(), self.spotter.create_stream()]

    def process(self, samples, selected):
        channels = [0] if selected == 'left' else [1] if selected == 'right' else [0, 1]
        for channel in channels:
            # Fixed bounded gain helps the quiet microphones; never affects saved audio.
            pcm = np.clip(samples[:, channel] * 8., -1., 1.).astype(np.float32)
            stream = self.streams[channel]
            stream.accept_waveform(16000, pcm)
            while self.spotter.is_ready(stream):
                self.spotter.decode_stream(stream)
                if self.spotter.get_result(stream) == KEYWORD:
                    self.reset()
                    return 'left' if channel == 0 else 'right'
        return None


class WakeWord:
    def __init__(self, on_detect, settings_file, model_factory=KeywordModel, clock=time.monotonic):
        self.on_detect = on_detect
        self.settings_file = Path(settings_file)
        self.model_factory = model_factory
        self.clock = clock
        self.lock = threading.RLock()
        self.queue = queue.Queue(maxsize=32)
        self.stop = threading.Event()
        self.thread = None
        self.generation = 0
        self.recording = False
        self.ready = False
        self.pending = False
        self.cooldown_until = 0.
        self.last_audio = None
        self.enabled = True
        try:
            value = json.loads(self.settings_file.read_text(encoding='utf-8')).get('wakeword_enabled')
            if isinstance(value, bool):self.enabled = value
        except (OSError, ValueError, AttributeError):pass
        self.data = dict(keyword=KEYWORD, engine='sherpa-onnx · CPU', error='',
                         detections=0, last_detected=0., source='', queue_drops=0,
                         cpu_one_core_percent=0.)

    def start(self):
        if self.thread is None:
            self.thread = threading.Thread(target=self.run, name='ffv-wakeword', daemon=True)
            self.thread.start()

    def invalidate(self, cooldown=False):
        with self.lock:
            self.generation += 1
            self.pending = False
            self.last_audio = None
            if cooldown:self.cooldown_until = self.clock() + 3.
            while True:
                try:self.queue.get_nowait()
                except queue.Empty:break

    def set_enabled(self, enabled):
        with self.lock:
            self.settings_file.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.settings_file.with_suffix('.tmp')
            temporary.write_text(json.dumps({'wakeword_enabled': bool(enabled)}), encoding='utf-8')
            temporary.replace(self.settings_file)
            self.enabled = bool(enabled)
            self.invalidate()

    def set_recording(self, active):
        with self.lock:
            self.recording = bool(active)
            self.invalidate(cooldown=not active)

    def offer(self, seq, samples, selected):
        with self.lock:
            self.last_audio = self.clock()
            if not self.ready or not self.enabled or self.recording or self.pending or self.clock() < self.cooldown_until:
                return
            item = (self.generation, seq, samples.copy(), selected, self.clock())
            try:self.queue.put_nowait(item)
            except queue.Full:
                self.data['queue_drops'] += 1
                self.invalidate()

    def consume(self, generation):
        """Claim a detection on the network owner, rejecting stale toggle/stop events."""
        with self.lock:
            if generation != self.generation or not self.enabled or self.recording or not self.pending:
                return False
            self.pending = False
            self.cooldown_until = self.clock() + 3.
            return True

    def snapshot(self):
        with self.lock:
            status = ('error' if self.data['error'] else 'loading' if not self.ready else
                      'recognizing' if self.recording else 'disabled' if not self.enabled else
                      'waiting-audio' if self.last_audio is None or self.clock() - self.last_audio > 1. else
                      'cooldown' if self.clock() < self.cooldown_until else
                      'detected' if self.pending else 'armed')
            return dict(self.data, enabled=self.enabled, status=status)

    def run(self):
        try:
            model = self.model_factory()
            with self.lock:self.ready = True
            epoch = last = mode = None
            cpu = duration = 0.
            while not self.stop.is_set():
                try:generation, seq, samples, selected, enqueued = self.queue.get(timeout=.2)
                except queue.Empty:continue
                with self.lock:
                    if generation != self.generation:continue
                if self.clock() - enqueued > .5:
                    self.invalidate()
                    continue
                if generation != epoch or selected != mode or last is None or seq != ((last + 1) & 0xffffffff):
                    model.reset()
                epoch, last, mode = generation, seq, selected
                start = time.thread_time()
                source = model.process(samples, selected)
                cpu += time.thread_time() - start
                duration += len(samples) / 16000
                event = None
                with self.lock:
                    self.data['cpu_one_core_percent'] = round(100 * cpu / max(duration, .001), 2)
                    if source and generation == self.generation and self.enabled and not self.recording and not self.pending:
                        self.pending = True
                        self.data.update(detections=self.data['detections'] + 1,
                                         last_detected=time.time(), source=source)
                        event = generation
                if event is not None:self.on_detect(event)
        except Exception as exc:
            with self.lock:self.data['error'] = str(exc)[:240]
