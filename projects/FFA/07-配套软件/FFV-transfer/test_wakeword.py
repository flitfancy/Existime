import json
import tempfile
import threading
import time
import unittest
from pathlib import Path

import numpy as np

from wakeword import WakeWord


class WakeWordTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.settings = Path(self.tmp.name) / 'voice-settings.json'
        self.events = []
        self.now = 10.
        self.wake = WakeWord(self.events.append, self.settings, clock=lambda: self.now)
        self.wake.ready = True

    def tearDown(self):
        self.wake.stop.set()
        if self.wake.thread:self.wake.thread.join(2)
        self.tmp.cleanup()

    def test_disable_persists_and_rejects_pending_hit(self):
        self.wake.pending = True
        epoch = self.wake.generation
        self.wake.set_enabled(False)
        self.assertFalse(self.wake.consume(epoch))
        self.assertFalse(WakeWord(self.events.append, self.settings).enabled)
        self.assertEqual(json.loads(self.settings.read_text()), {'wakeword_enabled': False})

    def test_manual_recording_and_stop_invalidate_hit(self):
        self.wake.pending = True
        epoch = self.wake.generation
        self.wake.set_recording(True)
        self.assertFalse(self.wake.consume(epoch))
        self.wake.offer(0, np.zeros((160, 2)), 'both')
        self.assertTrue(self.wake.queue.empty())
        self.wake.set_recording(False)
        self.wake.offer(1, np.zeros((160, 2)), 'both')
        self.assertTrue(self.wake.queue.empty())
        self.now += 3.1
        self.wake.offer(2, np.zeros((160, 2)), 'both')
        self.assertEqual(self.wake.queue.qsize(), 1)

    def test_queue_overflow_discards_incomplete_keyword(self):
        epoch = self.wake.generation
        for seq in range(33):self.wake.offer(seq, np.zeros((160, 2)), 'both')
        self.assertTrue(self.wake.queue.empty())
        self.assertGreater(self.wake.generation, epoch)
        self.assertEqual(self.wake.snapshot()['queue_drops'], 1)

    def test_disable_during_inference_cannot_start_recognition(self):
        entered, release = threading.Event(), threading.Event()
        class DelayedModel:
            def reset(self):pass
            def process(self, samples, selected):
                entered.set();release.wait(2)
                return 'left'
        self.wake.model_factory = DelayedModel
        self.wake.start()
        self.wake.offer(0, np.zeros((160, 2)), 'both')
        self.assertTrue(entered.wait(2))
        self.wake.set_enabled(False)
        release.set()
        self.wake.stop.set();self.wake.thread.join(2)
        self.assertEqual(self.events, [])

    def test_detection_consumed_only_once(self):
        self.wake.pending = True
        self.assertTrue(self.wake.consume(self.wake.generation))
        self.assertFalse(self.wake.consume(self.wake.generation))

    def test_network_owner_starts_real_capture_and_honors_stale_event(self):
        import app
        from unittest.mock import patch
        with patch.object(app, 'wakeword', self.wake), patch.dict(app.state, model='ready', recording=False), patch.object(app, 'request_wake_feedback'):
            self.wake.pending = True
            app.session_commands.put(('wake', self.wake.generation))
            capture = app.apply_session_commands(None)
            self.assertIsInstance(capture, app.MicCapture)
            self.assertTrue(app.state['recording'])
            capture.stop()
            self.wake.pending = True
            app.session_commands.put(('wake', self.wake.generation))
            app.stop_capture()
            self.assertIsNone(app.apply_session_commands(None))
        app.capture_stop.clear()


if __name__ == '__main__':unittest.main()
