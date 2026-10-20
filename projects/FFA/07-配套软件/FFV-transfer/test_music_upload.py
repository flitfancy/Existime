"""Real upload handlers with isolated scheduler state and no hardware or models."""
import asyncio
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from fastapi import FastAPI, HTTPException
from starlette.requests import Request, ClientDisconnect

from output_scheduler import OutputScheduler
from playback_routes import PlaybackAPI


def request_bytes(body):
    messages = iter([{'type': 'http.request', 'body': body, 'more_body': False}])
    async def receive():
        return next(messages)
    return Request({'type': 'http', 'headers': [(b'content-length', str(len(body)).encode())]}, receive)


class MusicUploadTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.state = {'recording': True}
        self.scheduler = OutputScheduler(self.state)
        self.scheduler.connected()
        self.api = PlaybackAPI(FastAPI(), self.scheduler, self.root, Mock(), Mock(return_value=([], '')))

    async def test_complete_upload_is_queued_with_exact_bytes(self):
        with patch('playback_routes.threading.Thread') as worker:
            result = await self.api.play_file(request_bytes(b'RIFFdata'), 'complete.wav')
            self.assertEqual(result['bytes'], 8)
            self.assertEqual(self.state['upload_received'], 8)
            self.assertFalse(self.state['upload_active'])
            worker.return_value.start.assert_called_once()
            upload = worker.call_args.kwargs['args'][1]
            self.assertEqual(upload.read_bytes(), b'RIFFdata')

    async def test_stalled_upload_times_out_and_cleans_up(self):
        async def receive():
            await asyncio.Event().wait()
        request = Request({'type': 'http', 'headers': [(b'content-length', b'1000')]}, receive)
        self.api.idle_timeout = .02
        with self.assertRaises(HTTPException) as error:
            await self.api.play_file(request, 'stalled.wav')
        self.assertEqual(error.exception.status_code, 408)
        self.assertFalse(self.state['playing'])
        self.assertFalse(self.state['upload_active'])
        self.assertTrue(self.state['play_error'])
        self.assertEqual(list(self.root.iterdir()), [])

    async def test_disconnect_releases_upload_and_reports_failure(self):
        messages = iter([
            {'type': 'http.request', 'body': b'RIFFpartial', 'more_body': True},
            {'type': 'http.disconnect'},
        ])
        async def receive():
            return next(messages)
        request = Request({'type': 'http', 'headers': [(b'content-length', b'1000')]}, receive)
        with self.assertRaises(ClientDisconnect):
            await self.api.play_file(request, 'interrupted.wav')
        self.assertFalse(self.state['playing'])
        self.assertFalse(self.state['upload_active'])
        self.assertNotEqual(self.state['play_status'], '接收文件')
        self.assertTrue(self.state['play_error'])
        self.assertEqual(list(self.root.iterdir()), [])

    async def test_concurrent_requests_do_not_overwrite_accepted_playback(self):
        entered, release = asyncio.Event(), asyncio.Event()
        async def receive():
            entered.set()
            await release.wait()
            return {'type': 'http.request', 'body': bytes(960), 'more_body': False}
        first = asyncio.create_task(self.api.play(Request({'type': 'http', 'headers': []}, receive), 'first'))
        await entered.wait()
        with self.assertRaises(HTTPException) as error:
            await self.api.play(request_bytes(bytes(960)), 'second')
        self.assertEqual(error.exception.status_code, 409)
        release.set()
        await first
        self.assertTrue(self.state['playing'])
        self.assertEqual(self.state['play_name'], 'first')
        self.assertEqual(self.scheduler.take().name, 'first')

    async def test_stop_stalled_upload_allows_new_play_without_old_cleanup_clobbering_it(self):
        entered = asyncio.Event()
        async def receive():
            entered.set()
            await asyncio.Event().wait()
        pending = asyncio.create_task(self.api.play_file(
            Request({'type': 'http', 'headers': []}, receive), 'old.wav'))
        await entered.wait()
        self.api.stop()
        await self.api.play(request_bytes(bytes(960)), 'new')
        with self.assertRaises(HTTPException) as error:
            await asyncio.wait_for(pending, 1)
        self.assertEqual(error.exception.status_code, 409)
        self.assertEqual(self.state['play_name'], 'new')
        self.assertTrue(self.state['playing'])
        self.assertEqual(self.state['play_error'], '')
        self.assertTrue(self.state['recording'], 'stopping output must not stop recognition')
        self.assertEqual(list(self.root.iterdir()), [])

    async def test_invalid_tts_body_cannot_leave_a_queued_speaker_reservation(self):
        with self.assertRaises(HTTPException) as error:
            await self.api.stream_audio_output(request_bytes(b''))
        self.assertEqual(error.exception.status_code, 400)
        self.assertFalse(self.state['playing'])
        self.assertIsNone(self.scheduler.take())
        await self.api.play(request_bytes(bytes(960)), 'retry')
        self.assertTrue(self.state['playing'])

    async def test_board_disconnect_ends_pending_tts_request(self):
        request = asyncio.create_task(self.api.stream_audio_output(request_bytes(bytes(960))))
        while not self.scheduler.busy:
            await asyncio.sleep(0)
        self.scheduler.disconnected('network lost')
        with self.assertRaises(HTTPException):
            await asyncio.wait_for(request, 1)
        self.assertFalse(self.state['playing'])
        self.scheduler.connected()
        self.assertIsNone(self.scheduler.take())

    async def test_cancelled_http_task_cleans_up_its_own_upload(self):
        entered = asyncio.Event()
        async def receive():
            entered.set()
            await asyncio.Event().wait()
        pending = asyncio.create_task(self.api.play_file(
            Request({'type': 'http', 'headers': []}, receive), 'cancelled.wav'))
        await entered.wait()
        pending.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await pending
        self.assertFalse(self.scheduler.busy)
        self.assertEqual(list(self.root.iterdir()), [])

    async def test_late_decoder_result_cannot_replace_new_output(self):
        entered, release = threading.Event(), threading.Event()
        def decoder(*args, **kwargs):
            entered.set()
            release.wait(2)  # Deliberately ignore cancellation to simulate a late callback.
            return bytes(960), {'decoded_duration': .01}
        self.api.decoder = decoder
        old = self.scheduler.reserve('file', 'old')
        work = self.root / 'old-work'
        work.mkdir()
        worker = threading.Thread(target=self.api.decode_and_play, args=(old, work / 'input.wav', work))
        worker.start()
        try:
            self.assertTrue(await asyncio.to_thread(entered.wait, 1))
            self.api.stop()
            await self.api.play(request_bytes(bytes(960)), 'new')
            release.set()
            await asyncio.to_thread(worker.join, 1)
            self.assertFalse(worker.is_alive())
            self.assertEqual(self.scheduler.take().name, 'new')
            self.assertTrue(self.state['playing'])
            self.assertEqual(self.state['play_error'], '')
            self.assertFalse(work.exists())
        finally:
            release.set()
            worker.join(2)


if __name__ == '__main__':
    unittest.main()
