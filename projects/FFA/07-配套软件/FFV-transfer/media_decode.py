"""Decode ordinary audio and locally owned NCM files to FFA PCM."""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from ring_protocol import MAX_SECONDS, MUSIC_RATE, SUPPORTED_RATES

ROOT = Path(__file__).resolve().parent
FFMPEG = Path(os.environ.get('FFV_FFMPEG', shutil.which('ffmpeg') or 'ffmpeg'))
FFPROBE = Path(os.environ.get('FFV_FFPROBE', shutil.which('ffprobe') or 'ffprobe'))
NCM_CONVERTER = ROOT / "third_party" / "pyNCMDUMP" / "ncm_converter.py"
AV3A_FFMPEG = ROOT / "third_party" / "av3a" / "ffmpeg.exe"
AUDIO_SUFFIXES = frozenset({'.flac', '.mp3', '.wav', '.aac', '.m4a', '.ogg', '.opus'})


def _run(command: list[str], timeout: int = 600, cancelled=None) -> subprocess.CompletedProcess:
    if cancelled is None:
        return subprocess.run(command, capture_output=True, check=True, timeout=timeout)
    if cancelled.is_set():
        raise InterruptedError('音频解码已取消')
    # communicate drains both pipes. Polling lets STOP release the actual decoder,
    # as well as the logical speaker reservation, even on Windows.
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        deadline = time.monotonic() + timeout
        try:
            while True:
                if cancelled.is_set():
                    raise InterruptedError('音频解码已取消')
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise subprocess.TimeoutExpired(command, timeout)
                try:
                    stdout, stderr = process.communicate(timeout=min(.1, remaining))
                    break
                except subprocess.TimeoutExpired:
                    continue
        except BaseException:
            process.kill()
            process.communicate()
            raise
        result = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
        result.check_returncode()
        return result


def _error_text(exc: Exception) -> str:
    data = getattr(exc, "stderr", None) or getattr(exc, "stdout", None) or b""
    return data.decode("utf-8", errors="replace").strip() if isinstance(data, bytes) else str(data).strip()


def unwrap_ncm(source: Path, work_dir: Path, *, cancelled=None) -> Path:
    before = set(work_dir.iterdir())
    try:
        _run(
            [
                sys.executable,
                "-u",
                "-X",
                "utf8",
                str(NCM_CONVERTER),
                str(source),
                "--target-folder",
                str(work_dir),
            ],
            timeout=120,
            cancelled=cancelled,
        )
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        detail = _error_text(exc)
        raise RuntimeError(f"NCM 解包失败：{detail[-500:]}") from exc
    produced = sorted(path for path in set(work_dir.iterdir()) - before if path.is_file())
    candidates = [path for path in produced if path.suffix.lower() in AUDIO_SUFFIXES]
    if len(candidates) != 1:
        raise RuntimeError(f"NCM 解包没有产生唯一音频文件：{[p.name for p in produced]}")
    return candidates[0]


def probe(source: Path, *, cancelled=None) -> dict:
    try:
        result = _run(
            [
                str(FFPROBE), "-v", "error", "-select_streams", "a:0",
                "-show_entries", "format=duration,format_name:stream=codec_name,codec_tag_string,sample_rate,channels,bits_per_raw_sample",
                "-of", "json", str(source),
            ],
            timeout=30,
            cancelled=cancelled,
        )
        data = json.loads(result.stdout.decode("utf-8"))
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, json.JSONDecodeError) as exc:
        detail = _error_text(exc)
        raise RuntimeError(f"无法识别音频格式：{detail[-500:]}") from exc
    streams = data.get("streams") or []
    if not streams:
        raise RuntimeError("文件中没有音频轨")
    stream = streams[0]
    codec = stream.get('codec_name')
    if not codec or codec in {'unknown', 'none'}:
        tag = stream.get('codec_tag_string') or '未知'
        if tag.lower() == 'av3a':
            codec = 'av3a'
        else:
            raise RuntimeError(f'当前解码器不支持音轨编码：{tag}')
    format_info = data.get("format") or {}
    return {
        "codec": codec,
        "sample_rate": int(stream.get("sample_rate") or 0),
        "channels": int(stream.get("channels") or 0),
        "bits": int(stream.get("bits_per_raw_sample") or 0),
        "container": format_info.get("format_name"),
        "duration": float(format_info.get("duration") or 0),
    }


def _decode_av3a(source: Path, work_dir: Path, rate: int, *, cancelled=None) -> bytes:
    if not AV3A_FFMPEG.is_file():
        raise RuntimeError('本机尚未安装 AV3A 解码器，请按 third_party/av3a/README.md 配置')
    # This native library writes debug text to stdout, even with -loglevel error.
    # A dedicated file keeps it out of PCM. TemporaryDirectory cleans partial output
    # after success, cancellation and failure, once _run has reaped the child.
    with tempfile.TemporaryDirectory(prefix='av3a-', dir=work_dir) as folder:
        output = Path(folder) / 'audio.pcm'
        try:
            _run([
                str(AV3A_FFMPEG), '-nostdin', '-hide_banner', '-loglevel', 'error',
                '-i', str(source), '-map', '0:a:0', '-vn',
                '-t', str(MAX_SECONDS + 1), '-ac', '1', '-ar', str(rate),
                '-c:a', 'pcm_s16le', '-f', 's16le', '-y', str(output),
            ], timeout=600, cancelled=cancelled)
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            raise RuntimeError(f'AV3A 解码失败：{_error_text(exc)[-500:]}') from exc
        if cancelled is not None and cancelled.is_set():
            raise InterruptedError('音频解码已取消')
        if not output.is_file():
            raise RuntimeError('AV3A 解码器没有产生音频数据')
        if output.stat().st_size > rate * MAX_SECONDS * 2:
            raise RuntimeError(f'解码结果超过当前 {MAX_SECONDS // 60} 分钟上限')
        return output.read_bytes()


def decode_to_pcm(upload: Path, work_dir: Path, *, rate: int = MUSIC_RATE, cancelled=None) -> tuple[bytes, dict]:
    if rate not in SUPPORTED_RATES:
        raise ValueError(f"不支持的播放采样率：{rate}")
    decoded_source = unwrap_ncm(upload, work_dir, cancelled=cancelled) if upload.suffix.lower() == ".ncm" else upload
    metadata = probe(decoded_source, cancelled=cancelled)
    if metadata["duration"] > MAX_SECONDS + 0.5:
        raise RuntimeError(f"音频超过当前 {MAX_SECONDS // 60} 分钟上限")
    if metadata['codec'] == 'av3a':
        pcm = _decode_av3a(decoded_source, work_dir, rate, cancelled=cancelled)
    else:
        try:
            result = _run(
                [
                    str(FFMPEG), "-hide_banner", "-loglevel", "error", "-i", str(decoded_source),
                    "-map", "0:a:0", "-vn", "-ac", "1", "-ar", str(rate),
                    "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1",
                ],
                timeout=600,
                cancelled=cancelled,
            )
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
            detail = _error_text(exc)
            raise RuntimeError(f"FFmpeg 解码失败：{detail[-500:]}") from exc
        pcm = result.stdout
    if len(pcm) % 2 or not pcm:
        raise RuntimeError("解码后的 PCM 数据无效")
    frames = len(pcm) // 2
    if frames > rate * MAX_SECONDS:
        raise RuntimeError(f"解码结果超过当前 {MAX_SECONDS // 60} 分钟上限")
    metadata.update(
        decoded_frames=frames,
        decoded_duration=frames / rate,
        decoded_rate=rate,
        decoded_channels=1,
        decoded_format="s16le",
        decoded_backend='av3a-native' if metadata['codec'] == 'av3a' else 'ffmpeg',
        ncm=upload.suffix.lower() == ".ncm",
    )
    return pcm, metadata
