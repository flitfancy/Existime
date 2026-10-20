"""Local speech HTTP adapters. Recognition shares the board's single ASR worker."""
import asyncio
from concurrent.futures import Future
from dataclasses import dataclass, field
from pathlib import Path
import queue
import subprocess
import tempfile
import time

import numpy as np
from fastapi import HTTPException, Request
from fastapi.responses import Response

from media_decode import FFMPEG
from character_voice import character_voice, load_config

ROOT = Path(__file__).resolve().parent
MAX_AUDIO_BYTES = 16 * 1024 * 1024
MAX_AUDIO_SECONDS = 120


@dataclass
class TranscriptionJob:
    audio: np.ndarray
    future: Future = field(default_factory=Future)
    rate: int = 16000


def decode_voice(path):
    result = subprocess.run([
        str(FFMPEG), '-nostdin', '-v', 'error', '-protocol_whitelist', 'file,pipe',
        '-format_whitelist', 'wav,mp3,flac,ogg,aac,mov,matroska,webm,amr', '-i', str(path),
        '-t', str(MAX_AUDIO_SECONDS + 1), '-vn', '-ac', '1', '-ar', '16000',
        '-f', 's16le', 'pipe:1',
    ], capture_output=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode or not result.stdout:
        raise ValueError('无法解码语音文件')
    if len(result.stdout) > MAX_AUDIO_SECONDS * 16000 * 2:
        raise ValueError('语音最长 120 秒')
    return np.frombuffer(result.stdout, dtype='<i2').astype(np.float32) / 32768


def synthesize_temporary(text, pcm=False):
    """Replaceable, offline Windows voice for transport tests, not the final character voice."""
    with tempfile.TemporaryDirectory(prefix='ffv-tts-') as folder:
        folder = Path(folder)
        source, target = folder / 'text.txt', folder / 'speech.wav'
        source.write_text(text, encoding='utf-8')
        subprocess.run([
            r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
            '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
            '-File', str(ROOT / 'temporary_voice.ps1'),
            '-TextPath', str(source), '-OutputPath', str(target),
        ], check=True, capture_output=True, timeout=120,
            creationflags=subprocess.CREATE_NO_WINDOW)
        if not pcm:
            return target.read_bytes()
        result = subprocess.run([
            str(FFMPEG), '-nostdin', '-v', 'error', '-i', str(target),
            '-ac', '1', '-ar', '48000', '-f', 's16le', 'pipe:1',
        ], check=True, capture_output=True, timeout=30,
            creationflags=subprocess.CREATE_NO_WINDOW)
        return result.stdout


def synthesize_voice(text, pcm=False):
    config = load_config()
    if config['engine'] == 'gpt-sovits':
        return character_voice.synthesize(text, pcm, config), 'gpt-sovits'
    return synthesize_temporary(text, pcm), 'windows-temporary'


def register_speech_routes(app, jobs, state, events):
    # Separate bounded requests prevent large uploads or TTS bursts from taking
    # over the event loop; the ASR queue remains the sole model owner.
    asr_slots = asyncio.Semaphore(2)
    tts_slot = asyncio.Semaphore(1)
    bridge = {'last_seen': 0, 'phase': 'offline', 'session': '', 'text': '', 'error': ''}
    state['dialogue'] = bridge
    tts_state = {'phase': 'idle', 'engine': load_config()['engine'], 'last_seconds': 0, 'error': ''}
    state['tts'] = tts_state

    @app.get('/v1/voice/bridge')
    async def bridge_status():
        return dict(bridge, connected=time.time() - bridge['last_seen'] < 30)

    @app.post('/v1/voice/bridge')
    async def bridge_update(request: Request):
        import json
        raw = bytearray()
        async for chunk in request.stream():
            raw.extend(chunk)
            if len(raw) > 32768:
                raise HTTPException(413, '桥接状态过大')
        try:
            data = json.loads(raw)
            phase = data.get('phase', 'heartbeat')
            if phase not in ('heartbeat', 'ready', 'thinking', 'text', 'playing', 'complete', 'error'):
                raise ValueError()
            for key in ('session', 'text', 'error'):
                if key in data and (not isinstance(data[key], str) or len(data[key]) > 6000):
                    raise ValueError()
        except (ValueError, TypeError, AttributeError):
            raise HTTPException(400, '桥接状态无效') from None
        bridge['last_seen'] = time.time()
        if data.get('protocol') == 2:bridge['protocol'] = 2
        if phase != 'heartbeat':
            bridge.update({key: data.get(key, '') for key in ('session', 'text', 'error')}, phase=phase)
            events.publish('assistant.reply' if phase in ('text', 'playing', 'complete') else 'assistant.state', dict(bridge))
        return {'ok': True}

    @app.post('/v1/audio/transcriptions')
    async def transcribe(request: Request):
        if state.get('model') != 'ready':
            raise HTTPException(503, '本地识别模型尚未就绪')
        if asr_slots.locked():
            raise HTTPException(429, '语音识别请求过多')
        async with asr_slots:
            # Bound the entire body before multipart parsing, including chunked requests.
            body = bytearray()
            async for chunk in request.stream():
                body.extend(chunk)
                if len(body) > MAX_AUDIO_BYTES:
                    raise HTTPException(413, '语音文件过大')
            request._body = bytes(body)
            with tempfile.TemporaryDirectory(prefix='ffv-stt-') as folder:
                path = Path(folder) / 'audio'
                async with request.form(max_files=1, max_fields=8) as form:
                    upload = form.get('file')
                    if upload is None or not hasattr(upload, 'read'):
                        raise HTTPException(400, '需要 multipart file 字段')
                    path.write_bytes(await upload.read())
                try:
                    audio = await asyncio.to_thread(decode_voice, path)
                except (ValueError, subprocess.SubprocessError) as exc:
                    raise HTTPException(400, '无法解码音频或音频超过 120 秒') from exc
            job = TranscriptionJob(audio)
            try:
                jobs.put_nowait(job)
            except queue.Full:
                raise HTTPException(429, '识别队列已满') from None
            try:
                text = await asyncio.wait_for(asyncio.wrap_future(job.future), 180)
            except asyncio.TimeoutError:
                raise HTTPException(504, '语音识别超时') from None
            except Exception as exc:
                raise HTTPException(502, '本地语音识别失败') from exc
            return {'text': text}

    @app.post('/v1/audio/speech')
    async def speech(request: Request):
        raw = bytearray()
        async for chunk in request.stream():
            raw.extend(chunk)
            if len(raw) > 16384:
                raise HTTPException(413, '合成请求过大')
        import json
        try:
            data = json.loads(raw)
            text = data.get('input', '').strip()
            fmt = data.get('response_format', 'wav')
        except (ValueError, AttributeError, TypeError):
            raise HTTPException(400, '需要 JSON input 文本') from None
        if not text or len(text) > 1500 or fmt not in ('wav', 'pcm'):
            raise HTTPException(400, 'input 长度需为 1–1500，格式为 wav 或 pcm')
        if tts_slot.locked():
            raise HTTPException(429, '语音合成正在使用中')
        async with tts_slot:
            started = time.monotonic()
            tts_state.update(phase='synthesizing', error='')
            try:
                audio, engine = await asyncio.to_thread(synthesize_voice, text, fmt == 'pcm')
            except Exception as exc:
                tts_state.update(phase='error', error=str(exc)[:240])
                raise HTTPException(502, '本地语音合成失败，请检查音频服务的 TTS 状态') from exc
            else:
                tts_state.update(phase='ready', engine=engine)
            finally:
                tts_state['last_seconds'] = round(time.monotonic() - started, 3)
        return Response(audio, media_type='audio/wav' if fmt == 'wav' else 'application/octet-stream',
                        headers={'X-Voice-Engine': engine, 'X-Audio-Sample-Rate': '48000',
                                 'X-Audio-Channels': '1', 'X-Audio-Format': 's16le'})
