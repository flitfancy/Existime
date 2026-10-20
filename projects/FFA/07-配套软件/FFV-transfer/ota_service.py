"""Bounded OTA upload and stop-and-wait transfer on the existing device session."""
from __future__ import annotations

import asyncio
import hashlib
import re
import struct
import threading
import time

from fastapi import HTTPException, Request
from protocol_v2 import FrameType

MAX_FIRMWARE_BYTES = 0x640000
IDENTITY = b'FFVOTA:S3:AUDIO:1\0'
INFO = struct.Struct('<HHI16s32s32s')
STATUS = struct.Struct('<BBHIII')
ACTIVE_PHASES = {'uploading', 'queued', 'writing', 'rebooting', 'verifying'}


def inspect_firmware(data: bytes) -> dict:
    if not 288 <= len(data) <= MAX_FIRMWARE_BYTES:
        raise ValueError('固件大小不符合板子的 OTA 分区')
    if data[0] != 0xe9 or struct.unpack_from('<H', data, 12)[0] != 9:
        raise ValueError('请选择 ESP32-S3 的应用固件，不能使用整片烧录包')
    if struct.unpack_from('<I', data, 32)[0] != 0xabcd5432:
        raise ValueError('固件缺少应用描述信息')
    if data[23] != 1 or hashlib.sha256(data[:-32]).digest() != data[-32:]:
        raise ValueError('固件 SHA-256 校验失败，文件可能不完整')
    position = data.find(IDENTITY)
    if position < 0 or position + len(IDENTITY) + 32 > len(data):
        raise ValueError('这不是支持 OTA 的 FIREFLY VOICE 固件')
    version = data[position + len(IDENTITY):position + len(IDENTITY) + 32].split(b'\0', 1)[0].decode('ascii', 'strict')
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:[-+][a-zA-Z0-9.-]+)?', version):
        raise ValueError('固件版本标识无效')
    return {'version': version, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'image_sha256': data[-32:].hex()}


def decode_info(payload: bytes) -> dict:
    if len(payload) != INFO.size:
        raise ValueError('无效固件信息')
    protocol, flags, capacity, partition, version, digest = INFO.unpack(payload)
    return {'protocol': protocol, 'pending_verify': bool(flags & 1), 'hardware_ready': bool(flags & 2),
            'capacity': capacity, 'partition': partition.split(b'\0')[0].decode('ascii'),
            'version': version.split(b'\0')[0].decode('ascii'), 'image_sha256': digest.hex()}


class OtaManager:
    def __init__(self):
        self.lock = threading.RLock()
        self.state = {'phase': 'idle', 'received': 0, 'total': 0, 'error': '', 'target_version': ''}
        self.firmware = None
        self.image = None
        self.metadata = None
        self.confirm_sequence = None
        self.verify_started = 0.
        self.health_started = 0.
        self.reboot_deadline = 0.
        self.transfer_claimed = False

    def snapshot(self):
        with self.lock:
            if (self.state['phase'] in {'rebooting', 'verifying'} and self.reboot_deadline
                    and time.monotonic() > self.reboot_deadline):
                self.fail('等待板子恢复超时，升级结果尚未确认；请检查供电和 Wi-Fi')
            return {**self.state, 'busy': self.state['phase'] in ACTIVE_PHASES,
                    'firmware': dict(self.firmware) if self.firmware else None}

    def begin_upload(self, size):
        with self.lock:
            if self.snapshot()['busy']:
                raise HTTPException(409, '固件升级已在进行')
            if not self.firmware or self.firmware['protocol'] != 1:
                raise HTTPException(409, '当前固件不支持 OTA，请先通过 USB 安装 OTA 基础固件')
            if self.firmware['pending_verify']:
                raise HTTPException(409, '当前固件仍在启动验证')
            if not 288 <= size <= min(MAX_FIRMWARE_BYTES, self.firmware['capacity']):
                raise HTTPException(413, '固件大小超过 OTA 分区或文件为空')
            self.state.update(phase='uploading', received=0, total=size, error='', target_version='')

    def fail(self, message):
        with self.lock:
            self.state.update(phase='error', error=str(message)[:300])
            self.image = None

    def queue_image(self, data):
        metadata = inspect_firmware(data)
        with self.lock:
            if not self.firmware or self.state['phase'] != 'uploading':
                raise ValueError('上传期间板子连接已变化，请等待重新连接后再试')
            if metadata['image_sha256'] == self.firmware['image_sha256']:
                raise ValueError('板子已经运行这份固件，无需重复升级')
            self.image, self.metadata = data, metadata
            self.transfer_claimed = False
            self.state.update(phase='queued', received=0, target_version=metadata['version'])

    def disconnected(self):
        with self.lock:
            self.firmware = None
            self.health_started = 0.
            self.confirm_sequence = None
            if self.state['phase'] in {'queued', 'writing'}:
                self.fail('升级连接中断，原固件仍保留；重新连接后可重试')

    def connected(self):
        self.health_started = time.monotonic()
        self.confirm_sequence = None

    def handle(self, frame):
        if frame.type == FrameType.FIRMWARE_INFO:
            with self.lock:
                self.firmware = decode_info(frame.payload)
                if self.state['phase'] in {'rebooting', 'verifying'}:
                    if self.firmware['image_sha256'] != self.metadata['image_sha256']:
                        self.fail('新固件未通过启动验证，板子已回到上一版固件')
                    elif not self.firmware['pending_verify']:
                        self.state.update(phase='complete', error='', received=self.state['total'])
                        self.image = None
                    else:
                        self.state['phase'] = 'verifying'
        elif frame.type == FrameType.OTA_STATUS and frame.sequence == self.confirm_sequence:
            stage, code, _, _, _, detail = STATUS.unpack(frame.payload)
            if stage != 4 or code:
                self.fail(f'固件启动确认失败 code={code}, detail={detail}')
            self.confirm_sequence = None

    def tick(self, connection, input_streaming, heartbeat_recent):
        now = time.monotonic()
        info = self.firmware
        if self.state['phase'] in {'rebooting', 'verifying'} and now > self.reboot_deadline:
            self.fail('等待新固件重新连接超时；板子会尝试回退，当前结果尚未确认')
        if info and info['pending_verify'] and not self.confirm_sequence:
            if now - self.health_started >= 5 and input_streaming and heartbeat_recent:
                if info['hardware_ready']:
                    self.confirm_sequence = connection.send_frame(FrameType.OTA_CONFIRM, bytes.fromhex(info['image_sha256']))
                else:
                    connection.send_frame(FrameType.FIRMWARE_QUERY)
                    self.health_started = now - 4

    def take_pending(self):
        with self.lock:
            if self.state['phase'] != 'queued' or self.transfer_claimed:
                return False
            self.transfer_claimed = True
            return True

    def transfer(self, connection, handle_frame):
        data = self.image
        if data is None:
            raise RuntimeError('OTA 文件已释放')
        self.state['phase'] = 'writing'

        def exchange(kind, payload=b'', expected_stage=1, expected_received=None, timeout=15):
            sequence = connection.send_frame(kind, payload)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                match = None
                for frame in connection.read_frames():
                    if frame.type == FrameType.OTA_STATUS and frame.sequence == sequence:
                        match = frame
                    else:
                        handle_frame(frame)
                if match is not None:
                    if len(match.payload) != STATUS.size:
                        raise RuntimeError('OTA 响应长度无效')
                    stage, code, _, received, total, detail = STATUS.unpack(match.payload)
                    if stage != expected_stage or code or total != len(data):
                        raise RuntimeError(f'板端拒绝升级 code={code}, detail={detail}')
                    if expected_received is not None and received != expected_received:
                        raise RuntimeError('板端 OTA 写入位置不一致')
                    self.state['received'] = received
                    return
                time.sleep(.002)
            raise TimeoutError('等待板端 OTA 写入确认超时')

        try:
            exchange(FrameType.OTA_BEGIN, struct.pack('<I', len(data)) + hashlib.sha256(data).digest(), expected_received=0)
            for offset in range(0, len(data), 4096):
                block = data[offset:offset + 4096]
                exchange(FrameType.OTA_DATA, struct.pack('<I', offset) + block, expected_received=offset + len(block))
            exchange(FrameType.OTA_END, expected_stage=2, expected_received=len(data), timeout=25)
            self.state['phase'] = 'rebooting'
            self.reboot_deadline = time.monotonic() + 120
            self.image = None
        except Exception as exc:
            try: connection.send_frame(FrameType.OTA_ABORT)
            except Exception: pass
            self.fail(exc)
            raise


def register_ota_routes(app, manager, state, activity_lock=None):
    @app.post('/device/firmware')
    async def upload_firmware(request: Request):
        try: size = int(request.headers.get('content-length') or 0)
        except ValueError: raise HTTPException(400, '非法文件长度') from None
        # Share admission with output reservations; neither may enter between
        # the other's idle check and reservation.
        from contextlib import nullcontext
        with activity_lock if activity_lock is not None else nullcontext():
            if not state.get('board_connected'):
                raise HTTPException(409, '板子当前未连接')
            if state.get('playing') or state.get('recording'):
                raise HTTPException(409, '请先停止播放和语音识别，再升级固件')
            manager.begin_upload(size)
        data = bytearray()
        try:
            stream = request.stream().__aiter__()
            while True:
                try: chunk = await asyncio.wait_for(stream.__anext__(), 30)
                except StopAsyncIteration: break
                if len(data) + len(chunk) > size:
                    raise ValueError('固件长度超出声明值')
                data.extend(chunk)
                manager.state['received'] = len(data)
            if len(data) != size:
                raise ValueError('固件上传不完整')
            manager.queue_image(bytes(data))
        except (Exception, asyncio.CancelledError) as exc:
            manager.fail(str(exc) or '固件上传连接中断')
            if isinstance(exc, ValueError): raise HTTPException(400, str(exc)) from None
            if isinstance(exc, TimeoutError): raise HTTPException(408, '固件上传超时') from None
            raise
        return {'accepted': True, **manager.metadata}
