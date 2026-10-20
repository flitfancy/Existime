"""Decode a local media file and play it through the Firefly Voice v2 transport."""

from __future__ import annotations

import argparse
import json
import shutil
import tempfile
import time
from pathlib import Path

from ffv_v2_client import FireflyVoiceConnection
from media_decode import decode_to_pcm
from protocol_v2 import FrameType
from output_scheduler import OutputScheduler, PCMSource
from output_session import OutputSession
from aec_runtime import RuntimeAEC


ROOT = Path(__file__).resolve().parent
PAIRING_KEY_FILE = ROOT / "media-work" / "wifi-pairing.key"
DONE_FLAG = 2


def main() -> None:
    arguments = argparse.ArgumentParser()
    arguments.add_argument("file", type=Path)
    arguments.add_argument("--host", default="192.168.1.40")
    arguments.add_argument("--rate", type=int, choices=(32000, 48000), default=48000)
    arguments.add_argument("--seconds", type=float)
    arguments.add_argument("--volume", type=int, choices=range(101), default=100)
    options = arguments.parse_args()
    source = options.file.resolve()
    if not source.is_file():
        raise FileNotFoundError(source)

    work_root = ROOT / "media-work"
    work_root.mkdir(exist_ok=True)
    work_dir = Path(tempfile.mkdtemp(prefix="ffv-v2-", dir=work_root))
    try:
        pcm, metadata = decode_to_pcm(source, work_dir, rate=options.rate)
        if options.seconds is not None:
            if options.seconds <= 0:
                raise ValueError("--seconds must be positive")
            pcm = pcm[: int(options.seconds * options.rate) * 2]
        frames_total = len(pcm) // 2
        print(json.dumps({"source": str(source), "metadata": metadata,
                          "played_seconds": frames_total / options.rate},
                         ensure_ascii=False, indent=2), flush=True)

        connection = FireflyVoiceConnection(
            options.host, pairing_key_file=PAIRING_KEY_FILE, mic_port=0
        )
        started = time.monotonic()
        try:
            # Reuse the same paced output state machine as the HTTP service.
            state = {'play_volume': options.volume}
            scheduler = OutputScheduler(state)
            scheduler.connected()
            task = scheduler.reserve('file', source.name)
            scheduler.submit(task, PCMSource(pcm, options.rate))
            output = OutputSession(scheduler.take(), scheduler, connection, RuntimeAEC().reference)
            last_ping = 0.0
            deadline = time.monotonic() + frames_total / options.rate + 15
            finished = False
            while time.monotonic() < deadline and not finished:
                for frame in connection.read_frames():
                    if frame.type == FrameType.ERROR:
                        raise RuntimeError(f'board v2 error: {frame.payload.hex()}')
                    if output.handle(frame):
                        finished = True
                if finished:
                    break
                output.drive()
                if time.monotonic() - last_ping >= 1:
                    connection.send_frame(FrameType.PING)
                    last_ping = time.monotonic()
                time.sleep(.002)
            if not finished:
                raise TimeoutError('playback did not reach the done state')
            print(json.dumps(state['result'], ensure_ascii=False, indent=2), flush=True)
        finally:
            connection.close()
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
