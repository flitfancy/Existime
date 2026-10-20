import threading
import unittest
from concurrent.futures import ThreadPoolExecutor

from output_scheduler import OutputCancelled, OutputScheduler, OutputUnavailable, PCMSource, SpeechStream


class OutputSchedulerTests(unittest.TestCase):
    def setUp(self):
        self.state = {}
        self.scheduler = OutputScheduler(self.state)
        self.scheduler.connected()

    def test_only_one_concurrent_reservation_can_change_state(self):
        gate = threading.Barrier(8)
        def request(index):
            gate.wait()
            try:
                return self.scheduler.reserve('file', str(index))
            except OutputUnavailable:
                return None
        with ThreadPoolExecutor(max_workers=8) as pool:
            accepted = [task for task in pool.map(request, range(8)) if task]
        self.assertEqual(len(accepted), 1)
        self.assertTrue(self.state['playing'])
        self.assertEqual(self.state['play_name'], accepted[0].name)

    def test_old_decoder_cannot_submit_update_or_cancel_new_task(self):
        old = self.scheduler.reserve('file', 'old')
        self.scheduler.cancel(old)
        new = self.scheduler.reserve('file', 'new')
        self.assertFalse(self.scheduler.update(old, play_status='old result'))
        self.assertFalse(self.scheduler.cancel(old, error='old error'))
        with self.assertRaises(OutputCancelled):
            self.scheduler.submit(old, PCMSource(bytes(16), 48000))
        self.assertTrue(self.state['playing'])
        self.assertEqual(self.state['output_task_id'], new.id)

    def test_active_stop_holds_speaker_until_acknowledged(self):
        task = self.scheduler.reserve('file', 'music')
        self.scheduler.submit(task, PCMSource(bytes(16), 48000))
        self.assertIs(self.scheduler.take(), task)
        self.scheduler.cancel()
        with self.assertRaises(OutputUnavailable):
            self.scheduler.reserve('speech', 'reply')
        self.scheduler.finish(task, '播放完成', result={'frames': 8})
        self.assertEqual(self.state['play_status'], '已停止')
        self.assertIsNone(self.state['result'])
        self.assertFalse(self.state['playing'])

    def test_disconnect_completes_pending_tts_and_does_not_replay(self):
        task = self.scheduler.reserve('speech', 'reply')
        stream = SpeechStream(48000)
        self.scheduler.submit(task, stream)
        self.scheduler.disconnected('network lost')
        self.assertTrue(stream.done.is_set())
        self.assertEqual(stream.error, 'network lost')
        self.assertTrue(task.done.is_set())
        self.assertFalse(self.state['playing'])
        self.scheduler.connected()
        self.assertIsNone(self.scheduler.take())

    def test_cancel_wakes_blocked_stream_writer(self):
        task = self.scheduler.reserve('speech', 'reply')
        stream = SpeechStream(48000)
        self.scheduler.submit(task, stream)
        stream.feed(bytes(32 * 8192))
        with ThreadPoolExecutor(max_workers=1) as pool:
            writer = pool.submit(stream.feed, bytes(8192))
            self.scheduler.cancel(task)
            with self.assertRaises(OutputCancelled):
                writer.result(timeout=1)

    def test_eof_does_not_block_on_a_full_queue_and_preserves_pcm(self):
        stream = SpeechStream(48000)
        data = bytes(range(256)) * 1024
        stream.feed(data)
        stream.finish_input()
        received = bytearray()
        while not stream.ended or stream.buffer:
            stream.drain_chunks()
            received.extend(stream.buffer)
            stream.buffer.clear()
        self.assertEqual(received, data)

    def test_offline_and_upgrade_reject_without_reserving(self):
        self.scheduler.disconnected()
        with self.assertRaises(OutputUnavailable):
            self.scheduler.reserve('file', 'offline')
        scheduler = OutputScheduler({}, blocked=lambda: True)
        scheduler.connected()
        with self.assertRaises(OutputUnavailable):
            scheduler.reserve('file', 'upgrade')
        self.assertFalse(scheduler.busy)


if __name__ == '__main__':
    unittest.main()
