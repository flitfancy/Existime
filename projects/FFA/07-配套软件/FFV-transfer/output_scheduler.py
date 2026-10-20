"""One owner for speaker reservations, cancellation and published playback state.

HTTP handlers and decoders hold task handles, never a process-wide stop flag.
Only DeviceSession claims tasks and talks to the board. An active cancellation
keeps its reservation until STOP is acknowledged or the connection is closed.
"""
from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
import threading
import uuid


class OutputUnavailable(RuntimeError):
    def __init__(self, message, status_code=409):
        super().__init__(message)
        self.status_code = status_code


class OutputCancelled(RuntimeError):
    pass


class SpeechStream:
    """Bounded producer/consumer PCM stream; cancellation wakes blocked writers."""
    def __init__(self, rate):
        self.rate = rate
        self.buffer = bytearray()  # Only the device worker consumes this buffer.
        self.ended = False
        self.done = threading.Event()
        self.error = ''
        self._condition = threading.Condition()
        self._chunks = deque()
        self._input_finished = False

    def feed(self, data):
        for offset in range(0, len(data), 8192):
            with self._condition:
                available = self._condition.wait_for(
                    lambda: len(self._chunks) < 32 or self.done.is_set(), timeout=30)
                if self.done.is_set():
                    raise OutputCancelled(self.error or '语音流已经结束')
                if self._input_finished:
                    raise ValueError('语音流输入已经关闭')
                if not available:
                    raise TimeoutError('语音输出缓冲等待超时')
                self._chunks.append(bytes(data[offset:offset + 8192]))

    def finish_input(self):
        with self._condition:
            self._input_finished = True
            self._condition.notify_all()

    def complete(self, error=''):
        with self._condition:
            if self.done.is_set():
                return
            self.error = str(error)
            self._chunks.clear()
            self.done.set()
            self._condition.notify_all()

    def drain_chunks(self):
        with self._condition:
            while self._chunks and len(self.buffer) < 32768:
                self.buffer.extend(self._chunks.popleft())
            self.ended = self._input_finished and not self._chunks
            self._condition.notify_all()


@dataclass(frozen=True)
class PCMSource:
    body: bytes
    rate: int

    @property
    def frames(self):
        return len(self.body) // 2


@dataclass(eq=False)
class OutputTask:
    kind: str
    name: str
    id: str = field(default_factory=lambda: uuid.uuid4().hex)
    cancelled: threading.Event = field(default_factory=threading.Event)
    done: threading.Event = field(default_factory=threading.Event)
    source: PCMSource | SpeechStream | None = None
    active: bool = False
    phase: str = 'preparing'
    error: str = ''
    stop_status: str = '已停止'
    pause_command: bool | None = None
    on_finish: object = None
    is_valid: object = None

    def check(self):
        if self.cancelled.is_set() or self.done.is_set():
            raise OutputCancelled(self.error or '音频任务已结束')


class OutputScheduler:
    def __init__(self, state, *, blocked=lambda: False):
        self.lock = threading.RLock()
        self._state = state
        self._blocked = blocked
        self._connected = False
        self._current = None
        self._keys = {
            'playing', 'play_paused', 'play_name', 'play_progress', 'play_position',
            'play_duration', 'play_status', 'play_error', 'play_volume', 'play_rate',
            'buffered_frames', 'ring_capacity_frames', 'retry_count', 'rebuffer_events',
            'metadata', 'result', 'lyrics', 'lyrics_source', 'upload_active',
            'upload_received', 'upload_total', 'output_mode', 'output_task_id', 'output_phase',
        }
        state.setdefault('play_volume', 100)
        state.update(output_task_id=None, output_phase='idle')

    def snapshot(self):
        with self.lock:
            return {key: self._state[key] for key in self._keys if key in self._state}

    @property
    def busy(self):
        with self.lock:
            return self._current is not None

    def connected(self):
        with self.lock:
            self._connected = True

    def disconnected(self, message='FIREFLY VOICE 连接中断'):
        with self.lock:
            self._connected = False
            task = self._current
            if task is not None:
                task.cancelled.set()
                self._finish(task, '播放失败', str(message))

    def reserve(self, kind, name, *, phase='preparing', status='准备音频', **fields):
        with self.lock:
            if not self._connected:
                raise OutputUnavailable('板子当前未连接', 503)
            if self._blocked():
                raise OutputUnavailable('固件正在升级')
            current = self._current
            # A pending cue never makes a user-requested file or TTS wait.
            if current and current.kind == 'cue' and not current.active and kind != 'cue':
                current.cancelled.set()
                self._finish(current, '提示音已跳过', '扬声器正在使用')
            if self._current is not None:
                raise OutputUnavailable('已有音频正在处理或播放')
            task = OutputTask(kind, name, phase=phase)
            self._current = task
            self._state.update(
                playing=True, play_paused=False, play_name=name, play_progress=0,
                play_position=0, play_duration=0, play_status=status, play_error='',
                buffered_frames=0, ring_capacity_frames=0, retry_count=0,
                rebuffer_events=0, metadata=None, result=None, lyrics=[], lyrics_source='',
                upload_active=False, upload_received=0, upload_total=0,
                output_mode='media' if kind == 'file' else kind,
                output_task_id=task.id, output_phase=phase,
            )
            self._state.update(fields)
            return task

    def cue(self, stream, turn):
        with self.lock:
            if turn.closed:
                return None
            try:
                task = self.reserve('cue', '唤醒提示音', status='唤醒提示音', play_duration=.3)
            except OutputUnavailable:
                return None
            task.on_finish = turn.cue_finished
            task.is_valid = lambda: not turn.closed
            turn.cue_started()
            self.submit(task, stream)
            return task

    def update(self, task, *, phase=None, **fields):
        with self.lock:
            if self._current is not task or task.cancelled.is_set() or task.done.is_set():
                return False
            if phase is not None:
                task.phase = phase
                fields['output_phase'] = phase
            self._state.update(fields)
            return True

    def submit(self, task, source):
        with self.lock:
            task.check()
            if self._current is not task:
                raise OutputCancelled('音频任务已失效')
            task.source = source
            self.update(task, phase='queued', upload_active=False, play_rate=source.rate)

    def take(self):
        with self.lock:
            task = self._current
            if not task or task.phase != 'queued' or self._blocked() or not self._connected:
                return None
            if task.is_valid and not task.is_valid():
                self.cancel(task, status='提示音已跳过')
                return None
            task.active = True
            self.update(task, phase='playing')
            return task

    def cancel(self, task=None, *, error='', status='已停止'):
        with self.lock:
            task = self._current if task is None else task
            if task is None or self._current is not task or task.done.is_set():
                return False
            if not task.cancelled.is_set():
                task.error = str(error)
                task.stop_status = status
                task.cancelled.set()
            if isinstance(task.source, SpeechStream):
                task.source.complete(task.error or '播放已停止')
            if task.active:
                task.phase = 'stopping'
                self._state.update(output_phase='stopping', play_status='正在停止', upload_active=False)
            else:
                self._finish(task, task.stop_status, task.error)
            return True

    def finish(self, task, status, error='', result=None):
        with self.lock:
            if self._current is not task or task.done.is_set():
                return False
            if task.cancelled.is_set():
                status, error, result = task.stop_status, task.error or error, None
            self._finish(task, status, str(error), result)
            return True

    def _finish(self, task, status, error='', result=None):
        task.error = error
        task.phase = 'failed' if error else 'cancelled' if task.cancelled.is_set() else 'complete'
        if isinstance(task.source, SpeechStream):
            task.source.complete(error or ('播放已停止' if task.cancelled.is_set() else ''))
        self._state.update(
            playing=False, play_paused=False, play_status=status, play_error=error,
            result=result, buffered_frames=0, ring_capacity_frames=0,
            upload_active=False, output_mode='idle', output_phase=task.phase,
        )
        self._current = None
        task.done.set()
        if task.on_finish:
            task.on_finish(error)

    def pause(self, paused):
        with self.lock:
            if self._current is None:
                raise OutputUnavailable('没有正在播放的音频')
            self._current.check()
            self._current.pause_command = bool(paused)

    def take_pause_command(self, task):
        with self.lock:
            command, task.pause_command = task.pause_command, None
            return command

    def set_volume(self, volume):
        with self.lock:
            self._state['play_volume'] = volume

    @property
    def volume(self):
        with self.lock:
            return int(self._state['play_volume'])
