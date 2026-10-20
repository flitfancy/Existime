"""Board-owned PCM sender. Transport framing and send-ahead budgets live here."""
import time

from protocol_v2 import BufferStatus, FrameType, PlayStart

PLAY_BLOCK_SAMPLES = 4096


class OutputSession:
    def __init__(self, task, scheduler, connection, reference):
        task.check()
        self.task = task
        self.scheduler = scheduler
        self.source = task.source
        self.kind = task.kind
        self.connection = connection
        self.reference = reference
        self.rate = self.source.rate
        self.total = self.source.frames if self.kind == 'file' else 0
        self.sent = self.played = self.capacity = self.rebuffers = 0
        self.ready = self.end_sent = self.cancel_sent = False
        self.paused = self.pause_requested = self.finished = False
        self.volume = None
        self.started = time.monotonic()
        self.stop_deadline = None
        reference.begin(self.rate)
        self.sync_volume()
        self.start_sequence = connection.send_frame(
            FrameType.PLAY_START,
            PlayStart(self.rate, 1, 2, 3000 if self.kind == 'file' else 160, self.total).pack(),
            flags=0 if self.kind == 'file' else 1,
        )

    def sync_volume(self):
        volume = self.scheduler.volume
        if volume != self.volume:
            self.connection.send_frame(FrameType.SET_VOLUME, bytes((volume,)))
            self.volume = volume
            self.reference.invalidate()

    def drive(self):
        if self.finished:
            return
        if self.task.cancelled.is_set():
            if not self.cancel_sent:
                self.connection.send_frame(FrameType.STOP)
                self.cancel_sent = True
                self.stop_deadline = time.monotonic() + 4
            elif time.monotonic() > self.stop_deadline:
                raise TimeoutError('等待板端停止确认超时')
            return
        self.sync_volume()
        paused = self.scheduler.take_pause_command(self.task)
        if paused is True and not (self.paused or self.pause_requested):
            self.connection.send_frame(FrameType.PAUSE)
            self.pause_requested = True
        elif paused is False and (self.paused or self.pause_requested):
            self.connection.send_frame(FrameType.RESUME)
            self.pause_requested = False
        if not self.ready:
            if time.monotonic() - self.started > 4:
                raise TimeoutError('等待板端准备超时')
            return
        if self.paused or self.pause_requested:
            return
        # Stay below ring capacity so control frames remain readable when paused.
        window = min(self.capacity, int(self.rate * (6 if self.kind == 'file' else .6)))
        for _ in range(2):
            if self.task.cancelled.is_set():
                return
            budget = max(0, window - (self.sent - self.played))
            if self.kind == 'file':
                count = min(PLAY_BLOCK_SAMPLES, budget, self.total - self.sent)
                body = self.source.body[self.sent * 2:(self.sent + count) * 2]
                ended = self.sent == self.total
            else:
                self.source.drain_chunks()
                if self.source.ended and len(self.source.buffer) % 2:
                    raise ValueError('TTS PCM16 数据长度必须是 2 字节的整数倍')
                count = min(PLAY_BLOCK_SAMPLES, budget, len(self.source.buffer) // 2)
                body = bytes(self.source.buffer[:count * 2])
                ended = self.source.ended and not self.source.buffer
            if count:
                self.connection.send_frame(FrameType.AUDIO_DATA, body)
                self.reference.append(body)
                if self.kind != 'file':
                    del self.source.buffer[:count * 2]
                self.sent += count
            elif ended and not self.end_sent:
                self.connection.send_frame(FrameType.PLAY_END)
                self.end_sent = True
            else:
                break

    def handle(self, frame):
        if self.finished:
            return True
        if frame.type not in (FrameType.BUFFER_STATUS, FrameType.PLAY_PROGRESS):
            return False
        if not self.ready:
            if frame.type != FrameType.BUFFER_STATUS or frame.sequence != self.start_sequence:
                return False
            self.ready = True
        snapshot = BufferStatus.unpack(frame.payload)
        self.played = snapshot.played_frames
        self.capacity = snapshot.capacity_frames
        self.reference.progress(self.played, frame.flags)
        self.scheduler.update(
            self.task, buffered_frames=snapshot.buffered_frames, ring_capacity_frames=self.capacity,
            play_position=round(self.played / self.rate, 3),
            play_progress=round(100 * self.played / self.total, 1) if self.total else 0,
        )
        if frame.type == FrameType.PLAY_PROGRESS:
            if frame.flags & 4:
                self.paused = True
                self.pause_requested = False
                self.scheduler.update(self.task, play_paused=True, play_status='已暂停')
            elif frame.flags & 8:
                self.paused = self.pause_requested = False
                self.scheduler.update(self.task, play_paused=False, play_status='播放中')
            elif frame.flags & 16:
                self.rebuffers += 1
                self.scheduler.update(self.task, rebuffer_events=self.rebuffers, play_status='等待后续音频')
            elif frame.flags & 32:
                self.scheduler.cancel(self.task)
                self.finish('已停止')
                return True
            elif frame.flags & 2:
                if snapshot.accepted_frames != self.sent or self.played != self.sent:
                    raise RuntimeError('播放完成帧计数不一致')
                self.scheduler.update(self.task, play_progress=100)
                self.finish('播放完成', result={
                    'frames': self.sent, 'rate': self.rate, 'seconds': self.sent / self.rate,
                    'retry_count': 0, 'rebuffer_events': self.rebuffers,
                    'total_seconds': round(time.monotonic() - self.started, 3), 'protocol': 'FFVP',
                })
                return True
            elif not self.paused:
                self.scheduler.update(self.task, play_status='播放中')
        return False

    def finish(self, status, error='', result=None):
        if self.finished:
            return
        self.finished = True
        self.reference.finish()
        self.scheduler.finish(self.task, status, error, result)
