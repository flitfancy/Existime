"""HTTP input adapters for the single speaker scheduler."""
import asyncio
from pathlib import Path
import shutil
import tempfile
import threading
import time

from fastapi import HTTPException, Query, Request
from starlette.requests import ClientDisconnect

from media_decode import AUDIO_SUFFIXES
from output_scheduler import OutputCancelled, OutputUnavailable, PCMSource, SpeechStream


class PlaybackAPI:
    def __init__(self, app, scheduler, work_root, decoder, lyrics_loader, *, rate=48000):
        self.scheduler = scheduler
        self.work_root = work_root
        self.decoder = decoder
        self.lyrics_loader = lyrics_loader
        self.rate = rate
        self.max_seconds = 30 * 60
        self.max_upload_bytes = 500 * 1024 * 1024
        self.idle_timeout = 30
        self.suffixes = AUDIO_SUFFIXES | {'.ncm'}
        for path, handler in (
            ('/play', self.play), ('/play-file', self.play_file),
            ('/v1/audio/output', self.stream_audio_output),
            ('/stop-playback', self.stop), ('/pause-playback', self.pause),
            ('/resume-playback', self.resume), ('/volume/{volume}', self.volume),
        ):
            app.add_api_route(path, handler, methods=['POST'])

    def reserve(self, *args, **kwargs):
        try:
            return self.scheduler.reserve(*args, **kwargs)
        except OutputUnavailable as exc:
            raise HTTPException(exc.status_code, str(exc)) from None

    @staticmethod
    def failure(exc):
        if isinstance(exc, (ClientDisconnect, asyncio.CancelledError)):
            return '音频上传连接中断，请重新提交'
        if isinstance(exc, TimeoutError):
            return '音频传输等待超时，请重新提交'
        return str(getattr(exc, 'detail', None) or exc) or '音频处理失败'

    @staticmethod
    def raise_http(exc):
        if isinstance(exc, TimeoutError):
            raise HTTPException(408, PlaybackAPI.failure(exc)) from None
        if isinstance(exc, OutputCancelled):
            raise HTTPException(409, str(exc)) from None
        raise exc

    async def chunks(self, request, task):
        """Observe task cancellation even when a client stops sending its body."""
        stream = request.stream().__aiter__()
        while True:
            task.check()
            pending = asyncio.create_task(stream.__anext__())
            deadline = time.monotonic() + self.idle_timeout
            try:
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError('音频上传停滞')
                    done, _ = await asyncio.wait((pending,), timeout=min(.1, remaining))
                    task.check()
                    if done:
                        try:
                            chunk = pending.result()
                        except StopAsyncIteration:
                            return
                        yield chunk
                        break
            finally:
                if not pending.done():
                    pending.cancel()
                await asyncio.gather(pending, return_exceptions=True)

    async def play(self, request: Request, name: str = 'audio'):
        safe_name = Path(name).name[:100] or 'audio'
        task = self.reserve('file', safe_name, phase='uploading', status='接收 PCM 音频')
        body = bytearray()
        try:
            async for chunk in self.chunks(request, task):
                if len(body) + len(chunk) > self.rate * 2 * self.max_seconds:
                    raise HTTPException(413, f'Audio is limited to {self.max_seconds} seconds')
                body.extend(chunk)
            if not body or len(body) % 2:
                raise HTTPException(400, 'Expected little-endian PCM16 mono data')
            source = PCMSource(bytes(body), self.rate)
            self.scheduler.update(task, play_duration=source.frames / self.rate,
                                  play_status='发送到板子并播放', metadata=self.metadata(self.rate))
            self.scheduler.submit(task, source)
        except (Exception, asyncio.CancelledError) as exc:
            self.scheduler.cancel(task, error=self.failure(exc), status='播放失败')
            self.raise_http(exc)
        return {'started': True, 'name': safe_name, 'seconds': round(source.frames / self.rate, 2),
                'full_duplex': True, 'task_id': task.id}

    async def play_file(self, request: Request, filename: str = Query(min_length=1, max_length=240)):
        suffix = Path(filename).suffix.lower()
        if suffix not in self.suffixes:
            raise HTTPException(400, '不支持的音频格式')
        try:
            expected = int(request.headers.get('content-length') or 0)
        except ValueError:
            raise HTTPException(400, '非法的 Content-Length') from None
        if expected < 0 or expected > self.max_upload_bytes:
            raise HTTPException(413, '文件超过 500 MB 上限')
        task = self.reserve('file', Path(filename).name, phase='uploading', status='接收文件',
                            upload_active=True, upload_total=expected)
        work_dir = None
        handed_off = False
        received = 0
        try:
            work_dir = Path(tempfile.mkdtemp(prefix='ffa-', dir=self.work_root))
            upload = work_dir / ('input' + suffix)
            lyrics, lyrics_source = self.lyrics_loader(filename)
            self.scheduler.update(task, lyrics=lyrics, lyrics_source=lyrics_source)
            with upload.open('wb') as destination:
                async for chunk in self.chunks(request, task):
                    received += len(chunk)
                    if received > self.max_upload_bytes:
                        raise HTTPException(413, '文件超过 500 MB 上限')
                    destination.write(chunk)
                    self.scheduler.update(task, upload_received=received)
            if not received:
                raise HTTPException(400, '文件为空')
            if expected and received != expected:
                raise HTTPException(400, '音频文件传输不完整，请重新拖入')
            task.check()
            self.scheduler.update(task, phase='decoding', upload_active=False, play_status='本机解包/解码中')
            worker = threading.Thread(target=self.decode_and_play, args=(task, upload, work_dir),
                                      name='ffv-decode-' + task.id[:8], daemon=True)
            worker.start()
            handed_off = True
        except (Exception, asyncio.CancelledError) as exc:
            self.scheduler.cancel(task, error=self.failure(exc), status='文件接收失败')
            self.raise_http(exc)
        finally:
            if work_dir is not None and not handed_off:
                shutil.rmtree(work_dir, ignore_errors=True)
        return {'accepted': True, 'filename': task.name, 'bytes': received, 'task_id': task.id}

    def decode_and_play(self, task, upload, work_dir):
        try:
            task.check()
            pcm, metadata = self.decoder(upload, work_dir, rate=self.rate, cancelled=task.cancelled)
            task.check()
            self.scheduler.update(task, metadata=metadata, play_duration=metadata.get('decoded_duration', 0),
                                  play_status='连接板子并预缓存')
            self.scheduler.submit(task, PCMSource(pcm, self.rate))
        except Exception as exc:
            if not task.cancelled.is_set():
                self.scheduler.cancel(task, error=self.failure(exc), status='播放失败')
                print('AUDIO_DECODE ' + repr(exc), flush=True)
        finally:
            shutil.rmtree(work_dir, ignore_errors=True)

    @staticmethod
    def metadata(rate):
        return {'decoded_rate': rate, 'decoded_channels': 1, 'decoded_format': 's16le'}

    async def stream_audio_output(self, request: Request, rate: int = 48000):
        if rate not in (32000, 48000):
            raise HTTPException(400, 'Rate must be 32000 or 48000')
        task = self.reserve('speech', '本地 AI TTS', status='等待 TTS 音频流', metadata=self.metadata(rate))
        speech = SpeechStream(rate)
        received = 0
        try:
            self.scheduler.submit(task, speech)
            async for chunk in self.chunks(request, task):
                received += len(chunk)
                if received > rate * 2 * 600:
                    raise HTTPException(413, 'TTS 音频流最长 10 分钟')
                for offset in range(0, len(chunk), 16384):
                    await asyncio.to_thread(speech.feed, chunk[offset:offset + 16384])
            if not received or received % 2:
                raise HTTPException(400, 'Expected little-endian PCM16 mono data')
            speech.finish_input()
            deadline = time.monotonic() + 660
            while not task.done.is_set():
                if time.monotonic() > deadline:
                    raise HTTPException(504, '等待 TTS 播放完成超时')
                await asyncio.sleep(.05)
            if task.error:
                raise HTTPException(502, task.error)
            if task.cancelled.is_set():
                raise OutputCancelled('播放已停止')
        except (Exception, asyncio.CancelledError) as exc:
            self.scheduler.cancel(task, error=self.failure(exc), status='播放失败')
            self.raise_http(exc)
        return {'played': True, 'bytes': received, 'frames': received // 2, 'rate': rate, 'task_id': task.id}

    def stop(self):
        self.scheduler.cancel()
        return {'stopping': True}

    def set_paused(self, paused):
        try:
            self.scheduler.pause(paused)
        except (OutputUnavailable, OutputCancelled) as exc:
            raise HTTPException(409, str(exc)) from None
        return {'pausing' if paused else 'resuming': True}

    def pause(self):
        return self.set_paused(True)

    def resume(self):
        return self.set_paused(False)

    def volume(self, volume: int):
        if not 0 <= volume <= 100:
            raise HTTPException(400, 'Volume must be 0..100')
        self.scheduler.set_volume(volume)
        return {'volume': volume, 'applies_immediately': self.scheduler.busy}
