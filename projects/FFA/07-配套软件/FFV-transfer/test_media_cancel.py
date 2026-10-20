"""Cancellation of real decoder child processes, without audio devices."""
import subprocess
import sys
import threading
import time
import unittest

from media_decode import _run


class DecoderProcessTests(unittest.TestCase):
    def test_cancel_terminates_running_decoder_promptly(self):
        cancelled = threading.Event()
        timer = threading.Timer(.15, cancelled.set)
        started = time.monotonic()
        timer.start()
        try:
            with self.assertRaises(InterruptedError):
                _run([sys.executable, '-c', 'import time; time.sleep(30)'], cancelled=cancelled)
        finally:
            timer.join()
        self.assertLess(time.monotonic() - started, 3)

    def test_cancelled_before_start_does_not_launch_child(self):
        cancelled = threading.Event()
        cancelled.set()
        with self.assertRaises(InterruptedError):
            _run(['nonexistent-decoder.exe'], cancelled=cancelled)

    def test_polling_communicate_preserves_both_pipes(self):
        code = 'import sys,time; sys.stdout.buffer.write(b"a"*200000); sys.stdout.flush(); time.sleep(.2); sys.stderr.buffer.write(b"b"*200000); sys.stdout.buffer.write(b"c"*200000)'
        result = _run([sys.executable, '-c', code], cancelled=threading.Event())
        self.assertEqual(result.stdout, b'a' * 200000 + b'c' * 200000)
        self.assertEqual(result.stderr, b'b' * 200000)

    def test_timeout_reaps_child(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            _run([sys.executable, '-c', 'import time; time.sleep(30)'], timeout=.05,
                 cancelled=threading.Event())


if __name__ == '__main__':
    unittest.main()
