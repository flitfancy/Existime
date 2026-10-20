"""HTTP -> scheduler -> paced sender -> board telemetry, all without hardware."""
import asyncio
from collections import deque
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

from fastapi import FastAPI
import httpx

import app as runtime
from output_scheduler import OutputScheduler, PCMSource
from output_session import OutputSession
from playback_routes import PlaybackAPI
from protocol_v2 import BufferStatus, Frame, FrameType


class Board:
    def __init__(self):
        self.received = bytearray()
        self.replies = deque()
        self.sent = []

    def send_frame(self, kind, payload=b'', *, flags=0):
        sequence = len(self.sent) + 1
        self.sent.append(kind)
        frames = len(self.received) // 2
        status = lambda: BufferStatus(frames, frames, 0, 576000).pack()
        if kind == FrameType.PLAY_START:
            self.replies.append(Frame(FrameType.BUFFER_STATUS, sequence, status()))
        elif kind == FrameType.AUDIO_DATA:
            self.received.extend(payload)
            frames = len(self.received) // 2
            self.replies.append(Frame(FrameType.PLAY_PROGRESS, sequence, status()))
        elif kind in (FrameType.PLAY_END, FrameType.STOP, FrameType.PAUSE, FrameType.RESUME):
            flag = {FrameType.PLAY_END: 2, FrameType.STOP: 32, FrameType.PAUSE: 4, FrameType.RESUME: 8}[kind]
            self.replies.append(Frame(FrameType.PLAY_PROGRESS, sequence, status(), flag))
        return sequence


class PipelineTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.state = {'recording': True}
        self.scheduler = OutputScheduler(self.state)
        self.scheduler.connected()
        self.app = FastAPI()
        self.api = PlaybackAPI(self.app, self.scheduler, Path(self.temp.name), Mock(), Mock(return_value=([], '')))
        self.client = httpx.AsyncClient(transport=httpx.ASGITransport(app=self.app), base_url='http://localhost')
        self.board = Board()
        self.reference = Mock()
        self.output = None

    async def asyncTearDown(self):
        await self.client.aclose()

    async def pump(self):
        if self.output is None:
            task = self.scheduler.take()
            if task:
                self.output = OutputSession(task, self.scheduler, self.board, self.reference)
        if self.output:
            while self.board.replies:
                if self.output.handle(self.board.replies.popleft()):
                    self.output = None
                    break
            if self.output:
                self.output.drive()
        await asyncio.sleep(.001)

    async def test_http_tts_preserves_pcm_reference_and_waits_for_completion(self):
        pcm = bytes(range(256)) * 400
        async def body():
            for offset in range(0, len(pcm), 777):
                yield pcm[offset:offset + 777]
                await asyncio.sleep(0)
        request = asyncio.create_task(self.client.post('/v1/audio/output?rate=48000', content=body()))
        async with asyncio.timeout(4):
            while not request.done():
                await self.pump()
        response = await request
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual(response.json()['frames'], len(pcm) // 2)
        self.assertEqual(self.board.received, pcm)
        self.assertEqual(b''.join(call.args[0] for call in self.reference.append.call_args_list), pcm)
        self.assertFalse(self.state['playing'])
        self.assertTrue(self.state['recording'])
        self.assertEqual(self.state['output_mode'], 'idle')

    async def test_stop_ack_releases_reservation_and_does_not_send_more_audio(self):
        response = await self.client.post('/play?name=first', content=bytes(48000 * 2 * 30))
        self.assertEqual(response.status_code, 200)
        await self.pump()
        await self.client.post('/stop-playback')
        rejected = await self.client.post('/play?name=too-early', content=bytes(960))
        self.assertEqual(rejected.status_code, 409)
        await self.pump()  # STOP is sent.
        stop_index = self.board.sent.index(FrameType.STOP)
        await self.pump()  # STOP is acknowledged.
        self.assertNotIn(FrameType.AUDIO_DATA, self.board.sent[stop_index + 1:])
        self.assertFalse(self.state['playing'])
        accepted = await self.client.post('/play?name=next', content=bytes(960))
        self.assertEqual(accepted.status_code, 200)
        self.assertEqual(self.state['play_name'], 'next')

    async def test_stop_timeout_requires_connection_reset_before_next_output(self):
        task = self.scheduler.reserve('file', 'old')
        self.scheduler.submit(task, PCMSource(bytes(960), 48000))
        self.output = OutputSession(self.scheduler.take(), self.scheduler, self.board, self.reference)
        self.scheduler.cancel(task)
        self.output.drive()
        with patch('output_session.time.monotonic', return_value=self.output.stop_deadline + .01):
            with self.assertRaises(TimeoutError):
                self.output.drive()
        self.assertTrue(self.scheduler.busy)
        self.scheduler.disconnected('stop acknowledgement lost')
        self.assertFalse(self.scheduler.busy)

    async def test_device_disconnect_cancels_unclaimed_tts(self):
        from output_scheduler import SpeechStream
        task = self.scheduler.reserve('speech', 'pending')
        stream = SpeechStream(48000)
        self.scheduler.submit(task, stream)
        with patch.object(runtime, 'output_scheduler', self.scheduler), patch.object(runtime, 'state', self.state), \
                patch.object(runtime, 'ota_manager', Mock()), patch.object(runtime, 'aec_runtime', Mock()), \
                patch.object(runtime, 'vad_observer', Mock()), patch.object(runtime, 'wakeword', Mock()):
            runtime.DeviceSession().disconnect('simulated disconnect')
        self.assertTrue(task.done.is_set())
        self.assertTrue(stream.done.is_set())
        self.assertFalse(self.state['playing'])
        self.scheduler.connected()
        self.assertIsNone(self.scheduler.take())


if __name__ == '__main__':
    unittest.main()
