"""Framed TCP protocol shared by FIREFLY VOICE control and speaker audio."""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum


MAGIC_BYTES = b"FFVP"
MAGIC = int.from_bytes(MAGIC_BYTES, "little")
VERSION = 1
MAX_PAYLOAD_BYTES = 8192

_HEADER_PREFIX = struct.Struct("<IBBHII")
_CHECKSUM = struct.Struct("<I")
HEADER_BYTES = _HEADER_PREFIX.size + _CHECKSUM.size


class ProtocolError(ValueError):
    """A complete frame violated the protocol contract."""


class FrameType(IntEnum):
    AUTH = 1
    AUTH_RESULT = 2
    PING = 3
    PONG = 4

    PLAY_START = 10
    AUDIO_DATA = 11
    PLAY_END = 12
    PAUSE = 13
    RESUME = 14
    STOP = 15
    SET_VOLUME = 16
    REBOOT = 17
    MIC_CONFIG = 18

    DEVICE_STATUS = 20
    BUFFER_STATUS = 21
    PLAY_PROGRESS = 22
    ERROR = 23
    BUTTON = 24
    FIRMWARE_QUERY = 30
    FIRMWARE_INFO = 31
    OTA_BEGIN = 32
    OTA_DATA = 33
    OTA_END = 34
    OTA_ABORT = 35
    OTA_STATUS = 36
    OTA_CONFIRM = 37


class PlayFlags(IntEnum):
    LIVE = 1


@dataclass(frozen=True, slots=True)
class Frame:
    type: FrameType
    sequence: int
    payload: bytes = b""
    flags: int = 0


@dataclass(frozen=True, slots=True)
class PlayStart:
    rate: int
    channels: int
    sample_bytes: int
    prebuffer_ms: int
    total_frames: int

    _STRUCT = struct.Struct("<IBBHI")

    def pack(self) -> bytes:
        if self.rate not in (32000, 48000):
            raise ProtocolError("sample rate must be 32000 or 48000")
        if self.channels != 1 or self.sample_bytes != 2:
            raise ProtocolError("speaker format must be PCM16 mono")
        if not 0 <= self.prebuffer_ms <= 5000:
            raise ProtocolError("prebuffer must be between 0 and 5000 ms")
        if not 0 <= self.total_frames <= self.rate * 60 * 30:
            raise ProtocolError("stream is longer than 30 minutes")
        return self._STRUCT.pack(
            self.rate,
            self.channels,
            self.sample_bytes,
            self.prebuffer_ms,
            self.total_frames,
        )

    @classmethod
    def unpack(cls, payload: bytes) -> "PlayStart":
        if len(payload) != cls._STRUCT.size:
            raise ProtocolError("invalid PLAY_START payload length")
        value = cls(*cls._STRUCT.unpack(payload))
        value.pack()
        return value


@dataclass(frozen=True, slots=True)
class BufferStatus:
    accepted_frames: int
    played_frames: int
    buffered_frames: int
    capacity_frames: int

    _STRUCT = struct.Struct("<IIII")

    def pack(self) -> bytes:
        values = (
            self.accepted_frames,
            self.played_frames,
            self.buffered_frames,
            self.capacity_frames,
        )
        if any(not 0 <= value <= 0xFFFFFFFF for value in values):
            raise ProtocolError("buffer counters must be uint32")
        return self._STRUCT.pack(*values)

    @classmethod
    def unpack(cls, payload: bytes) -> "BufferStatus":
        if len(payload) != cls._STRUCT.size:
            raise ProtocolError("invalid BUFFER_STATUS payload length")
        return cls(*cls._STRUCT.unpack(payload))


def encode_frame(
    frame_type: FrameType | int,
    sequence: int,
    payload: bytes = b"",
    *,
    flags: int = 0,
) -> bytes:
    try:
        kind = FrameType(frame_type)
    except ValueError as exc:
        raise ProtocolError(f"unknown frame type {frame_type}") from exc
    payload = bytes(payload)
    if not 0 <= sequence <= 0xFFFFFFFF:
        raise ProtocolError("sequence must be uint32")
    if not 0 <= flags <= 0xFFFF:
        raise ProtocolError("flags must be uint16")
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"payload exceeds {MAX_PAYLOAD_BYTES} bytes")
    prefix = _HEADER_PREFIX.pack(
        MAGIC,
        VERSION,
        int(kind),
        flags,
        sequence,
        len(payload),
    )
    checksum = zlib.crc32(prefix + payload) & 0xFFFFFFFF
    return prefix + _CHECKSUM.pack(checksum) + payload


class FrameParser:
    """Incrementally reconstruct frames from an arbitrarily split TCP stream."""

    def __init__(self, *, max_payload_bytes: int = MAX_PAYLOAD_BYTES):
        if max_payload_bytes <= 0:
            raise ValueError("max_payload_bytes must be positive")
        self.max_payload_bytes = max_payload_bytes
        self.buffer = bytearray()
        self.discarded_bytes = 0

    def feed(self, data: bytes) -> list[Frame]:
        self.buffer.extend(data)
        output: list[Frame] = []
        while True:
            position = self.buffer.find(MAGIC_BYTES)
            if position < 0:
                keep = min(len(self.buffer), len(MAGIC_BYTES) - 1)
                self.discarded_bytes += len(self.buffer) - keep
                if keep:
                    del self.buffer[:-keep]
                else:
                    self.buffer.clear()
                return output
            if position:
                del self.buffer[:position]
                self.discarded_bytes += position
            if len(self.buffer) < HEADER_BYTES:
                return output

            magic, version, raw_type, flags, sequence, length = _HEADER_PREFIX.unpack_from(self.buffer)
            checksum = _CHECKSUM.unpack_from(self.buffer, _HEADER_PREFIX.size)[0]
            if magic != MAGIC:
                raise AssertionError("magic search and decoded magic disagree")
            if version != VERSION:
                raise ProtocolError(f"unsupported protocol version {version}")
            try:
                frame_type = FrameType(raw_type)
            except ValueError as exc:
                raise ProtocolError(f"unknown frame type {raw_type}") from exc
            if length > self.max_payload_bytes:
                raise ProtocolError(f"payload length {length} exceeds limit")

            total = HEADER_BYTES + length
            if len(self.buffer) < total:
                return output
            prefix = bytes(self.buffer[: _HEADER_PREFIX.size])
            payload = bytes(self.buffer[HEADER_BYTES:total])
            actual = zlib.crc32(prefix + payload) & 0xFFFFFFFF
            if actual != checksum:
                raise ProtocolError(
                    f"frame CRC mismatch at sequence {sequence}: {actual:08x} != {checksum:08x}"
                )
            del self.buffer[:total]
            output.append(Frame(frame_type, sequence, payload, flags))
