import asyncio
import hashlib
import struct
import unittest
from unittest.mock import Mock
import time

from fastapi import FastAPI, HTTPException
from starlette.requests import Request, ClientDisconnect
from ota_service import OtaManager, inspect_firmware, register_ota_routes, IDENTITY, INFO, STATUS
from protocol_v2 import Frame, FrameType


def image(version='0.4.1', chip=9, marker=IDENTITY):
    data = bytearray(9000)
    data[0] = 0xe9; data[23] = 1
    struct.pack_into('<H', data, 12, chip)
    struct.pack_into('<I', data, 32, 0xabcd5432)
    data[512:512 + len(marker)] = marker
    data[512 + len(marker):512 + len(marker) + len(version)] = version.encode()
    return bytes(data) + hashlib.sha256(data).digest()


def manager_ready():
    manager = OtaManager()
    manager.handle(Frame(FrameType.FIRMWARE_INFO, 1,
        INFO.pack(1, 2, 0x640000, b'app0', b'0.4.0', bytes(32))))
    return manager


class FakeBoard:
    def __init__(self, reject=False):
        self.frames = []; self.sequence = 0; self.body = bytearray(); self.total = 0
        self.reject = reject; self.aborted = False; self.ended = False
    def send_frame(self, kind, payload=b''):
        self.sequence += 1; stage = 1; code = 0
        if kind == FrameType.OTA_BEGIN:
            self.total = struct.unpack_from('<I', payload)[0]; self.digest = payload[4:]
        elif kind == FrameType.OTA_DATA:
            assert struct.unpack_from('<I', payload)[0] == len(self.body)
            self.body.extend(payload[4:])
            if self.reject: stage = 255; code = 3
        elif kind == FrameType.OTA_END:
            assert hashlib.sha256(self.body).digest() == self.digest
            stage = 2; self.ended = True
        elif kind == FrameType.OTA_ABORT:
            self.aborted = True; return self.sequence
        self.frames.append(Frame(FrameType.OTA_STATUS, self.sequence,
            STATUS.pack(stage, code, 0, len(self.body), self.total, 0)))
        return self.sequence
    def read_frames(self):
        frames, self.frames = self.frames, []; return frames


class OtaTests(unittest.TestCase):
    def test_image_identity_chip_and_integrity(self):
        self.assertEqual(inspect_firmware(image())['version'], '0.4.1')
        for invalid in [image()[:-1], image(chip=0), image(marker=b'other-project-xx\0'), b'bad', image()[:500] + b'x' + image()[501:]]:
            with self.assertRaises(ValueError): inspect_firmware(invalid)

    def test_duplicate_and_concurrent_upload_rejected(self):
        m = manager_ready(); blob = image(); m.begin_upload(len(blob))
        with self.assertRaises(HTTPException): m.begin_upload(len(blob))
        m.firmware['image_sha256'] = blob[-32:].hex()
        with self.assertRaisesRegex(ValueError, '已经'): m.queue_image(blob)

    def test_transfer_waits_for_boot_and_confirmation(self):
        m = manager_ready(); blob = image(); m.begin_upload(len(blob)); m.queue_image(blob)
        board = FakeBoard(); m.transfer(board, lambda frame: None)
        self.assertEqual(bytes(board.body), blob); self.assertTrue(board.ended)
        self.assertEqual(m.snapshot()['phase'], 'rebooting')
        m.disconnected(); m.connected()
        m.handle(Frame(FrameType.FIRMWARE_INFO, 2, INFO.pack(1, 3, 0x640000, b'app1', b'0.4.1', blob[-32:])))
        self.assertEqual(m.snapshot()['phase'], 'verifying')
        m.handle(Frame(FrameType.FIRMWARE_INFO, 3, INFO.pack(1, 2, 0x640000, b'app1', b'0.4.1', blob[-32:])))
        self.assertEqual(m.snapshot()['phase'], 'complete')

    def test_rejection_aborts_without_committing(self):
        m = manager_ready(); blob = image(); m.begin_upload(len(blob)); m.queue_image(blob)
        board = FakeBoard(reject=True)
        with self.assertRaises(RuntimeError): m.transfer(board, lambda frame: None)
        self.assertTrue(board.aborted); self.assertFalse(board.ended)
        self.assertFalse(m.snapshot()['busy'])

    def test_old_image_after_reboot_reports_rollback(self):
        m = manager_ready(); blob = image(); m.begin_upload(len(blob)); m.queue_image(blob)
        m.transfer(FakeBoard(), lambda frame: None)
        m.handle(Frame(FrameType.FIRMWARE_INFO, 2, INFO.pack(1, 2, 0x640000, b'app0', b'0.4.0', bytes(32))))
        self.assertIn('上一版', m.snapshot()['error'])

    def test_boot_confirmation_requires_both_recent_input_and_heartbeat(self):
        m = manager_ready(); m.firmware['pending_verify'] = True
        m.health_started = time.monotonic() - 6
        connection = Mock()
        m.tick(connection, False, True)
        m.tick(connection, True, False)
        connection.send_frame.assert_not_called()
        m.tick(connection, True, True)
        connection.send_frame.assert_called_once_with(FrameType.OTA_CONFIRM, bytes(32))


class OtaUploadTests(unittest.IsolatedAsyncioTestCase):
    async def test_disconnected_upload_can_retry(self):
        app = FastAPI(); m = manager_ready()
        register_ota_routes(app, m, {'board_connected': True})
        endpoint = next(route.endpoint for route in app.routes if route.path == '/device/firmware')
        messages = iter([{'type': 'http.request', 'body': b'partial', 'more_body': True}, {'type': 'http.disconnect'}])
        async def receive(): return next(messages)
        with self.assertRaises(ClientDisconnect):
            await endpoint(Request({'type': 'http', 'headers': [(b'content-length', b'9000')]}, receive))
        self.assertFalse(m.snapshot()['busy'])
        m.begin_upload(9000)

    async def test_complete_http_upload_queues_exact_firmware(self):
        app = FastAPI(); m = manager_ready(); blob = image()
        register_ota_routes(app, m, {'board_connected': True})
        endpoint = next(route.endpoint for route in app.routes if route.path == '/device/firmware')
        async def receive(): return {'type': 'http.request', 'body': blob, 'more_body': False}
        result = await endpoint(Request({'type': 'http', 'headers': [(b'content-length', str(len(blob)).encode())]}, receive))
        self.assertEqual(result['version'], '0.4.1'); self.assertEqual(m.image, blob)


if __name__ == '__main__': unittest.main()
