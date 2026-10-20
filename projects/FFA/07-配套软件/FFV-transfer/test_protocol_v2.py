import random
import struct

from protocol_v2 import (
    HEADER_BYTES,
    MAX_PAYLOAD_BYTES,
    BufferStatus,
    Frame,
    FrameParser,
    FrameType,
    PlayFlags,
    PlayStart,
    ProtocolError,
    encode_frame,
)


def parse_in_chunks(wire: bytes, sizes: list[int]) -> tuple[list[Frame], FrameParser]:
    parser = FrameParser()
    frames: list[Frame] = []
    position = 0
    for size in sizes:
        frames.extend(parser.feed(wire[position : position + size]))
        position += size
    frames.extend(parser.feed(wire[position:]))
    return frames, parser


def expect_error(wire: bytes, message: str) -> None:
    try:
        FrameParser().feed(wire)
    except ProtocolError:
        return
    raise AssertionError(message)


def main() -> None:
    start = PlayStart(48000, 1, 2, 1000, 48000 * 200)
    status = BufferStatus(96000, 48000, 48000, 192000)
    audio = bytes((index * 37 + 11) & 0xFF for index in range(1920))
    expected = [
        Frame(FrameType.PLAY_START, 1, start.pack()),
        Frame(FrameType.AUDIO_DATA, 2, audio),
        Frame(FrameType.BUFFER_STATUS, 3, status.pack()),
        Frame(FrameType.PLAY_END, 4),
    ]
    wire = b"garbage-before-magic" + b"".join(
        encode_frame(frame.type, frame.sequence, frame.payload, flags=frame.flags)
        for frame in expected
    )

    one_byte_frames, parser = parse_in_chunks(wire, [1] * len(wire))
    assert one_byte_frames == expected
    assert parser.discarded_bytes == len(b"garbage-before-magic")
    assert not parser.buffer

    randomizer = random.Random(0xFFA2026)
    for _ in range(200):
        sizes = [randomizer.randint(1, 211) for _ in range(len(wire) // 50)]
        frames, _ = parse_in_chunks(wire, sizes)
        assert frames == expected

    live = PlayStart(32000, 1, 2, 180, 0)
    live_wire = encode_frame(FrameType.PLAY_START, 9, live.pack(), flags=PlayFlags.LIVE)
    assert FrameParser().feed(live_wire) == [
        Frame(FrameType.PLAY_START, 9, live.pack(), int(PlayFlags.LIVE))
    ]
    assert PlayStart.unpack(start.pack()) == start
    assert BufferStatus.unpack(status.pack()) == status
    assert HEADER_BYTES == 20

    corrupt = bytearray(encode_frame(FrameType.AUDIO_DATA, 20, audio))
    corrupt[-1] ^= 0x80
    expect_error(bytes(corrupt), "corrupt payload was accepted")

    oversized = bytearray(encode_frame(FrameType.PING, 21))
    struct.pack_into("<I", oversized, 12, MAX_PAYLOAD_BYTES + 1)
    expect_error(bytes(oversized), "oversized payload was accepted")

    unknown = bytearray(encode_frame(FrameType.PING, 22))
    unknown[5] = 255
    expect_error(bytes(unknown), "unknown frame type was accepted")

    print("protocol v2 framing, fragmentation, validation, and typed payloads ok")


if __name__ == "__main__":
    main()

