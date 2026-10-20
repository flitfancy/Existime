"""Reusable host connection for the Firefly Voice v2 transport."""

from __future__ import annotations

import select
import socket
import threading
import time
from pathlib import Path

from protocol_v2 import Frame, FrameParser, FrameType, encode_frame


class FireflyVoiceConnection:
    def __init__(
        self,
        host: str,
        port: int = 7866,
        *,
        pairing_key_file: Path,
        mic_port: int = 7867,
    ) -> None:
        self.host = host
        self._sequence = 1
        self._send_lock = threading.Lock()
        self._parser = FrameParser()
        self._pending_frames = []
        self.socket = socket.create_connection((host, port), timeout=3)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 256 * 1024)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.socket.settimeout(0.25)
        self.audio_socket: socket.socket | None = None
        try:
            key = pairing_key_file.read_bytes()
            if len(key) != 32:
                raise RuntimeError("本机 Wi-Fi 配对密钥无效")
            auth_sequence = self.send_frame(FrameType.AUTH, key)
            frames = self.wait_for(
                lambda frame: frame.type == FrameType.AUTH_RESULT
                and frame.sequence == auth_sequence,
                timeout=3,
            )
            auth = next(
                frame
                for frame in frames
                if frame.type == FrameType.AUTH_RESULT and frame.sequence == auth_sequence
            )
            if auth.payload != b"\0":
                raise RuntimeError("板子拒绝了 Wi-Fi 配对密钥")
            self._pending_frames.extend(frame for frame in frames if frame is not auth)

            self.audio_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.audio_socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
            self.audio_socket.bind((self.socket.getsockname()[0], mic_port))
            self.audio_socket.setblocking(False)
            self.socket.setblocking(False)
        except Exception:
            self.close()
            raise

    def send_frame(
        self,
        frame_type: FrameType | int,
        payload: bytes = b"",
        *,
        flags: int = 0,
    ) -> int:
        with self._send_lock:
            sequence = self._sequence
            self._sequence = (self._sequence + 1) & 0xFFFFFFFF
            packet = encode_frame(frame_type, sequence, payload, flags=flags)
            self.socket.settimeout(5)
            try:
                self.socket.sendall(packet)
            finally:
                self.socket.setblocking(False)
            return sequence

    def read_frames(self) -> list[Frame]:
        output, self._pending_frames = self._pending_frames, []
        while select.select([self.socket], [], [], 0)[0]:
            try:
                data = self.socket.recv(65536)
            except BlockingIOError:
                break
            if not data:
                raise ConnectionError("板子的 Wi-Fi 音频连接已关闭")
            output.extend(self._parser.feed(data))
        return output

    def wait_for(self, predicate, *, timeout: float) -> list[Frame]:
        frames: list[Frame] = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            frames.extend(self.read_frames())
            if any(predicate(frame) for frame in frames):
                return frames
            time.sleep(0.002)
        raise TimeoutError("等待板端 v2 状态超时")

    def read_audio(self):
        if not self.audio_socket:
            return None
        try:
            data, address = self.audio_socket.recvfrom(4096)
        except (BlockingIOError, OSError):
            return None
        if address[0] != self.host or len(data) < 16 or data[:4] != b"FFVA":
            return ("skip",)
        import struct
        import zlib

        _, sequence, size, flags, checksum = struct.unpack_from("<IIHHI", data)
        if size not in (960, 1920) or len(data) != 16 + size:
            return ("skip",)
        payload = data[16:]
        return ("audio", sequence, flags, checksum, payload)

    def close(self) -> None:
        if self.audio_socket:
            self.audio_socket.close()
            self.audio_socket = None
        if getattr(self, "socket", None):
            self.socket.close()
