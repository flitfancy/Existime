"""Lazy, persistent character TTS process with no additional listening port."""
import atexit
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import uuid
import wave

from media_decode import FFMPEG

ROOT = Path(__file__).resolve().parent
CONFIG = ROOT / 'data' / 'tts.local.json'


def load_config(path=CONFIG):
    if not path.is_file():
        return {'engine': 'windows-temporary'}
    config = json.loads(path.read_text(encoding='utf-8-sig'))
    if config.get('engine') not in ('gpt-sovits', 'windows-temporary'):
        raise ValueError('未知的本机 TTS 引擎')
    return config


class CharacterVoice:
    def __init__(self):
        self.lock = threading.RLock()
        self.process = None
        self.temp = None
        self.log = None
        self.config = None

    def close(self):
        with self.lock:
            if self.process is not None:
                try:
                    if self.process.poll() is None:
                        self.process.terminate()
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
                finally:
                    if self.process.stdin:
                        with contextlib.suppress(BrokenPipeError, OSError):
                            self.process.stdin.close()
                    self.process = None
            if self.log:
                self.log.close()
                self.log = None
            if self.temp:
                self.temp.cleanup()
                self.temp = None
            self.config = None

    def _wait(self, path, deadline):
        while time.monotonic() < deadline:
            if path.is_file():
                result = json.loads(path.read_text(encoding='utf-8'))
                if not result.get('ok'):
                    raise RuntimeError(result.get('error', '角色语音合成失败'))
                return result
            if self.process.poll() is not None:
                raise RuntimeError('角色语音进程已退出，请查看本机 tts-worker.log')
            time.sleep(.05)
        raise TimeoutError('角色语音合成超时')

    def _start(self, config, deadline):
        if self.process is not None and self.process.poll() is None and self.config == config:
            return
        self.close()
        root = Path(config['project_root'])
        python = root / 'runtime' / 'python.exe'
        if not python.is_file():
            raise ValueError('GPT-SoVITS Python 环境不存在')
        for key in ('t2s_weights_path', 'vits_weights_path', 'bert_base_path', 'cnhuhbert_base_path'):
            if not (root / config['inference'][key]).exists():
                raise ValueError('GPT-SoVITS 模型路径不存在：' + key)
        if not Path(config['synthesis']['ref_audio_path']).is_file():
            raise ValueError('参考音频不存在')
        run_root = ROOT / 'media-work'
        run_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='tts-worker-', dir=run_root)
        folder = Path(self.temp.name)
        snapshot = folder / 'config.json'
        snapshot.write_text(json.dumps(config, ensure_ascii=False), encoding='utf-8')
        (ROOT / 'data').mkdir(exist_ok=True)
        self.log = (ROOT / 'data' / 'tts-worker.log').open('ab')
        env = dict(os.environ, PYTHONUTF8='1', PYTHONIOENCODING='utf-8',
                   HF_HUB_OFFLINE='1', TRANSFORMERS_OFFLINE='1')
        env['PATH'] = str(python.parent) + os.pathsep + str(FFMPEG.parent) + os.pathsep + env.get('PATH', '')
        self.process = subprocess.Popen([
            str(python), '-s', '-u', '-X', 'utf8', str(ROOT / 'gpt_sovits_worker.py'),
            '--config', str(snapshot), '--run-dir', str(folder),
        ], cwd=root, stdin=subprocess.PIPE, stdout=self.log, stderr=self.log,
            text=True, encoding='utf-8', env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        self._wait(folder / 'ready.json', deadline)
        self.config = config

    def synthesize(self, text, pcm=False, config=None):
        config = load_config() if config is None else config
        with self.lock:
            deadline = time.monotonic() + 150
            try:
                self._start(config, deadline)
                job_id = uuid.uuid4().hex
                folder = Path(self.temp.name) / job_id
                folder.mkdir()
                try:
                    self.process.stdin.write(json.dumps({'id': job_id, 'text': text}, ensure_ascii=False) + '\n')
                    self.process.stdin.flush()
                    self._wait(folder / 'result.json', deadline)
                    result = subprocess.run([
                        str(FFMPEG), '-nostdin', '-v', 'error', '-i', str(folder / 'speech.wav'),
                        '-t', '601', '-ac', '1', '-ar', '48000', '-f', 's16le', 'pipe:1',
                    ], capture_output=True, check=True, timeout=20,
                        creationflags=subprocess.CREATE_NO_WINDOW)
                    audio = result.stdout
                    if not audio or len(audio) % 2 or len(audio) > 600 * 48000 * 2:
                        raise ValueError('角色语音输出无效或超过十分钟')
                finally:
                    # A timeout may leave the model writing: reap before directory cleanup.
                    if not (folder / 'result.json').is_file():
                        self.close()
                    elif folder.exists():
                        for path in folder.iterdir():
                            path.unlink()
                        folder.rmdir()
            except Exception:
                self.close()
                raise
            if pcm:
                return audio
            output = io.BytesIO()
            with wave.open(output, 'wb') as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes(audio)
            return output.getvalue()


character_voice = CharacterVoice()
atexit.register(character_voice.close)
