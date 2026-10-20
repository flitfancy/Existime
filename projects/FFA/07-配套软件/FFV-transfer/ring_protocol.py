"""Long-duration FFA playback using a board-side PSRAM ring buffer."""
from __future__ import annotations

import struct
import threading
import time
import zlib
from dataclasses import dataclass

import serial
from serial.tools import list_ports

BOARD_SERIAL = "80456b28175c"
RATE = 32000
MUSIC_RATE = 48000
SUPPORTED_RATES = (RATE, MUSIC_RATE)
MAX_SECONDS = 30 * 60
BLOCK_SAMPLES = 120
CONTROL_MAGIC = b"FFVC"

READY = 1
ACK = 2
UPLOAD_DONE = 3
PLAY_STARTED = 4
PLAY_DONE = 5
ERROR = 6
REBUFFER = 7
PAUSED = 8
RESUMED = 9
PROGRESS = 10

_serial_lock = threading.Lock()


@dataclass(frozen=True)
class Control:
    kind: int
    code: int
    value: int
    detail: int


def find_board_port() -> str:
    matches = [
        port.device
        for port in list_ports.comports()
        if (port.serial_number or "").replace(":", "").lower() == BOARD_SERIAL
    ]
    if len(matches) != 1:
        raise RuntimeError(f"FFA board was not found uniquely (matches={matches})")
    return matches[0]


class ControlReader:
    def __init__(self, connection: serial.Serial):
        self.connection = connection
        self.buffer = bytearray()
        self.pending: list[Control] = []

    def _read_one(self, deadline: float) -> Control:
        while time.monotonic() < deadline:
            chunk = self.connection.read(self.connection.in_waiting or 1)
            if chunk:
                self.buffer.extend(chunk)
            position = self.buffer.find(CONTROL_MAGIC)
            if position < 0:
                if len(self.buffer) > 3:
                    del self.buffer[:-3]
                continue
            if position:
                del self.buffer[:position]
            if len(self.buffer) < 16:
                continue
            _, kind, code, _, value, detail = struct.unpack_from("<IBBHII", self.buffer)
            del self.buffer[:16]
            return Control(kind, code, value, detail)
        raise TimeoutError("Timed out waiting for a board control frame")

    def expect(self, kind: int, timeout: float) -> Control:
        deadline = time.monotonic() + timeout
        while True:
            for index, control in enumerate(self.pending):
                if control.kind == ERROR:
                    self.pending.pop(index)
                    raise RuntimeError(
                        f"Board error code={control.code}, value={control.value}, detail={control.detail}"
                    )
                if control.kind == kind:
                    return self.pending.pop(index)
            control = self._read_one(deadline)
            if control.kind == ERROR:
                raise RuntimeError(
                    f"Board error code={control.code}, value={control.value}, detail={control.detail}"
                )
            if control.kind == kind:
                return control
            self.pending.append(control)

    def take_pending(self, kind: int) -> Control | None:
        for index, control in enumerate(self.pending):
            if control.kind == kind:
                return self.pending.pop(index)
        return None


def play_pcm_ring(pcm: bytes, progress=None, *, rate: int = RATE, volume: int = 100) -> dict:
    if len(pcm) % 2:
        raise ValueError("PCM16 payload has an odd byte count")
    if rate not in SUPPORTED_RATES:
        raise ValueError(f"Unsupported sample rate {rate}; expected one of {SUPPORTED_RATES}")
    if not 0 <= volume <= 100:
        raise ValueError("Volume must be between 0 and 100")
    frames = len(pcm) // 2
    if not 0 < frames <= rate * MAX_SECONDS:
        raise ValueError(f"Audio must be no longer than {MAX_SECONDS} seconds")
    whole_crc = zlib.crc32(pcm) & 0xFFFFFFFF

    with _serial_lock:
        port = find_board_port()
        connection = serial.Serial(
            port=port,
            baudrate=115200,
            timeout=0.05,
            write_timeout=3,
            dsrdtr=False,
            rtscts=False,
        )
        connection.dtr = False
        connection.rts = False
        reader = ControlReader(connection)
        started_at = time.monotonic()
        try:
            time.sleep(0.1)
            connection.reset_input_buffer()
            connection.write(b"V" + bytes((volume,)))
            connection.write(b"R" + struct.pack("<IIII", 0x4D525453, frames, rate, whole_crc))
            connection.flush()
            ready = reader.expect(READY, 4)

            sent = 0
            playback_started = None
            retry_count = 0
            rebuffer_events = 0
            upload_started = time.monotonic()
            while sent < frames:
                count = min(BLOCK_SAMPLES, frames - sent)
                block = pcm[sent * 2 : (sent + count) * 2]
                block_crc = zlib.crc32(block) & 0xFFFFFFFF
                packet = b"B" + struct.pack("<IHI", sent, count, block_crc) + block
                expected = sent + count
                for attempt in range(16):
                    connection.write(packet)
                    connection.flush()
                    try:
                        while True:
                            ack = reader.expect(ACK, 0.75)
                            if ack.value < expected:
                                continue
                            if ack.value > expected:
                                raise RuntimeError(f"ACK advanced to {ack.value}, expected {expected}")
                            break
                        break
                    except TimeoutError:
                        retry_count += 1
                        if attempt == 15:
                            raise
                sent = expected
                if playback_started is None:
                    playback_started = reader.take_pending(PLAY_STARTED)
                rebuffered = False
                while reader.take_pending(REBUFFER) is not None:
                    rebuffer_events += 1
                    rebuffered = True
                if progress:
                    progress(
                        "rebuffering" if rebuffered else ("streaming" if playback_started else "buffering"),
                        sent,
                        frames,
                        ack.detail,
                        ready.value,
                        None,
                    )

            upload = reader.expect(UPLOAD_DONE, 4)
            if upload.value != frames or upload.detail != whole_crc:
                raise RuntimeError("Whole-stream CRC or frame count mismatch")
            upload_seconds = time.monotonic() - upload_started
            if playback_started is None:
                playback_started = reader.expect(PLAY_STARTED, 4)
            if progress:
                progress("playing", sent, frames, 0, ready.value, None)

            done = reader.expect(PLAY_DONE, frames / rate + 5)
            while reader.take_pending(REBUFFER) is not None:
                rebuffer_events += 1
            result = {
                "port": port,
                "frames": frames,
                "rate": rate,
                "seconds": frames / rate,
                "crc32": f"{whole_crc:08x}",
                "ring_capacity_frames": ready.value,
                "prebuffer_frames": ready.detail,
                "upload_seconds": round(upload_seconds, 3),
                "audio_write_ms": done.detail,
                "retry_count": retry_count,
                "rebuffer_events": rebuffer_events,
                "total_seconds": round(time.monotonic() - started_at, 3),
            }
            if done.value != frames:
                raise RuntimeError(f"Board played {done.value} of {frames} frames")
            if progress:
                progress("done", frames, frames, 0, ready.value, result)
            return result
        except Exception:
            try:
                connection.write(b"X")
                connection.flush()
            except Exception:
                pass
            raise
        finally:
            connection.close()
