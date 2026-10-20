# FIREFLY VOICE TCP protocol v2

Protocol v2 carries speaker PCM and low-rate device control over one authenticated TCP
connection. Microphone PCM remains on UDP port 7867.

## Frame layout

OTA extension (firmware 0.4.x) uses the same authenticated connection:

| Value | Name | Payload |
| ---: | --- | --- |
| 30 | FIRMWARE_QUERY | empty |
| 31 | FIRMWARE_INFO | `<HHI16s32s32s>` OTA protocol, flags, slot capacity, slot label, version, running image SHA256 |
| 32 | OTA_BEGIN | `<I32s>` complete bin length, complete bin SHA256 |
| 33 | OTA_DATA | uint32 byte offset + at most 4096 bytes |
| 34 | OTA_END | empty; validate then change boot slot |
| 35 | OTA_ABORT | empty; discard incomplete transaction |
| 36 | OTA_STATUS | `<BBHIII>` stage, code, reserved, received, total, error detail; echoes request sequence |
| 37 | OTA_CONFIRM | running image SHA256 (32 bytes) |

FIRMWARE_INFO flags: 1 pending boot verification; 2 hardware initialized. OTA_STATUS stages:
1 writing, 2 rebooting, 3 aborted, 4 confirmed, 255 rejected. The running image digest excludes
the appended digest itself (ESP-IDF partition SHA256); OTA_BEGIN digest covers the entire file.
The firmware announces FIRMWARE_INFO after authentication; older firmware remains usable but
does not expose OTA. Uploads are accepted only in an idle window. See `OTA.md` for recovery rules.

All integer fields are little-endian. Every frame begins with a fixed 20-byte header.

| Offset | Field | Type | Meaning |
| ---: | --- | --- | --- |
| 0 | magic | 4 bytes | ASCII `FFVP` |
| 4 | version | uint8 | `1` |
| 5 | type | uint8 | `FrameType` value |
| 6 | flags | uint16 | type-specific flags |
| 8 | sequence | uint32 | sender-owned monotonic sequence |
| 12 | payload length | uint32 | 0 to 8192 bytes |
| 16 | CRC32 | uint32 | CRC of header bytes 0-15 followed by payload |
| 20 | payload | bytes | type-specific data |

The receiver reads and validates the complete header before consuming the declared payload.
An invalid version, type, length, or CRC terminates the current TCP session and audio stream.
PCM bytes are never interpreted as single-byte commands.

## Message types

| Value | Name | Direction | Payload |
| ---: | --- | --- | --- |
| 1 | AUTH | PC to board | 32-byte pairing key |
| 2 | AUTH_RESULT | Board to PC | uint8: 0 accepted, 1 denied; echoes request sequence |
| 3 | PING | PC to board | empty in production; arbitrary bytes for diagnostics |
| 4 | PONG | Board to PC | echoes PING payload, flags, and sequence |
| 10 | PLAY_START | PC to board | `PlayStart` |
| 11 | AUDIO_DATA | PC to board | PCM16 mono bytes |
| 12 | PLAY_END | PC to board | empty |
| 13 | PAUSE | PC to board | empty |
| 14 | RESUME | PC to board | empty |
| 15 | STOP | PC to board | empty |
| 16 | SET_VOLUME | PC to board | uint8, 0 to 100 |
| 17 | REBOOT | PC to board | empty |
| 18 | MIC_CONFIG | PC to board | `<BBH>` enabled, gain (1/2/4/8), destination UDP port |
| 20 | DEVICE_STATUS | Board to PC | `<IiIBBH>` uptime ms, RSSI, IP, connected, authenticated, reserved |
| 21 | BUFFER_STATUS | Board to PC | four uint32 frame counters |
| 22 | PLAY_PROGRESS | Board to PC | BufferStatus; flags 2 done, 4 paused, 8 resumed, 16 rebuffer, 32 stopped |
| 23 | ERROR | Board to PC | `<BBHII>` code, message context, reserved, value, detail |
| 24 | BUTTON | Board to PC | two uint8: event (1 single, 2 double, 3 lights), lights enabled |

`PlayStart` is `<IBBHI>`: sample rate, channels, sample bytes, requested prebuffer in
milliseconds, and total frames. A live stream sets the `LIVE` flag and uses zero total frames.

`BufferStatus` is `<IIII>`: accepted frames, played frames, buffered frames, and ring capacity
frames. It is periodic telemetry, not a per-block acknowledgement.
PLAY_START is answered once with BUFFER_STATUS echoing its sequence. Periodic status and
events use the board's sequence counter; sequences are not stream IDs. The host waits for a
terminal event before starting another stream. MIC_CONFIG always captures both physical mics;
left/right selection is done on the PC. UDP targets the authenticated TCP peer address.

## Flow control

The PC sends framed PCM continuously. The board writes it to a blocking ring buffer. When the
ring is full, TCP receive pressure propagates to the sender through the TCP receive window.
There is no application-level timeout retransmission. Periodic `BUFFER_STATUS` frames keep the
send-ahead amount bounded so pause and stop commands are not buried behind many seconds of PCM.
The production host sends at most six seconds ahead for files and 600 ms for live speech,
always capped by ring capacity, using `sent_frames - last_reported_played_frames`. Consumption
telemetry opens this window; no per-block acknowledgement or retransmission is used. File ring
capacity is up to 12 seconds and live capacity is 2 seconds. A stalled or invalid session is
closed and playback aborted. Only the firmware network task may touch WiFiClient, including
connected()/remoteIP(); output events cross a bounded FreeRTOS queue.
