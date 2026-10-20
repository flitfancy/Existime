"""A bounded wake-triggered instruction turn, independent of continuous recording."""
import math
import threading
import time

import numpy as np


def wake_chime(rate=48000):
    """Quiet, windowed two-note cue plus silence; it uses normal playback volume."""
    pcm = np.zeros(int(rate * .30), np.float32)
    length = int(rate * .09)
    envelope = np.sin(np.linspace(0, np.pi, length)) ** 2
    for offset, hz in [(0, 880), (int(rate * .12), 1320)]:
        pcm[offset:offset + length] = .06 * envelope * np.sin(2 * np.pi * hz * np.arange(length) / rate)
    return np.rint(pcm * 32767).astype('<i2').tobytes()


class VoiceTurn:
    wait_seconds = 8.
    maximum_seconds = 45.

    def __init__(self, session_id, clock=time.monotonic, on_complete=None):
        self.session_id = session_id
        self.clock = clock
        self.lock = threading.RLock()
        self.started = self.listen_started = clock()
        self.last_activity = self.started
        self.input_ready = True
        self.phase = 'waiting-speech'
        self.cue = 'visual'
        self.pending = 0
        self.closed = False
        self.requested_finish = False
        self.reason = ''
        self.texts = []
        self.error = ''
        self.on_complete = on_complete
        self.finalized = False
        self.emitted = False

    def cue_started(self):
        with self.lock:self.phase = 'cue';self.cue = 'sound';self.input_ready = False

    def cue_finished(self, error=''):
        with self.lock:
            if self.closed:return
            self.phase = 'waiting-speech'
            self.listen_started = self.clock()
            self.input_ready = True
            if error:self.cue = 'unavailable'

    def allows_audio(self):
        # The worker must drain audio queued before a manual stop/finish marker.
        with self.lock:return self.input_ready and not self.requested_finish

    def observe(self, active):
        with self.lock:
            if active and not self.closed and self.phase != 'cue':
                self.phase = 'listening';self.last_activity = self.clock()

    def queued(self):
        with self.lock:
            self.pending += 1
            if self.closed:self.phase = 'processing'

    def result(self, text='', error=''):
        with self.lock:
            self.pending = max(0, self.pending - 1)
            if text.strip():self.texts.append(text.strip())
            if error:self.error = str(error)[:240]
            if self.closed:self._settle()

    def request_finish(self):
        with self.lock:self.requested_finish = True

    def stop_reason(self):
        with self.lock:
            if self.requested_finish:return 'utterance'
            if self.clock() - self.started >= self.maximum_seconds:return 'limit'
            if self.phase == 'waiting-speech' and self.clock() - self.listen_started >= self.wait_seconds:
                return 'no-speech'
            if self.phase == 'listening' and self.clock() - self.last_activity >= 1.2:
                return 'utterance'
            return ''

    def close(self, reason):
        with self.lock:
            self.closed = True
            self.reason = reason
            self._settle()

    def _settle(self):
        if self.pending or not self.finalized:self.phase = 'processing'
        elif self.error:self.phase = 'error'
        elif self.texts:self.phase = 'complete'
        elif self.reason == 'no-speech':self.phase = 'timeout'
        elif self.reason in ('manual', 'disconnect'):self.phase = 'stopped'
        else:self.phase = 'unrecognized'
        if self.finalized and not self.pending and not self.emitted:
            self.emitted = True
            if self.on_complete:self.on_complete(self.snapshot())

    def finalize(self):
        """All audio before the finish marker has been submitted; no further jobs follow."""
        with self.lock:
            self.finalized = True
            if self.closed:self._settle()

    def snapshot(self):
        with self.lock:
            remaining = max(0, math.ceil(self.wait_seconds - (self.clock() - self.listen_started)))
            return dict(session=self.session_id, mode='wake', phase=self.phase, cue=self.cue,
                        reason=self.reason, pending=self.pending, text=''.join(self.texts),
                        error=self.error, wait_remaining=remaining if self.phase == 'waiting-speech' else None)
