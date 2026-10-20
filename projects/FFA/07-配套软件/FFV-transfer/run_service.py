"""Check external prerequisites without starting devices, or launch the audio service."""
import argparse
from importlib.metadata import PackageNotFoundError, version
import json
import os
from pathlib import Path
import runpy
import shutil

ROOT = Path(__file__).resolve().parent

def check():
    packages = {}
    missing = []
    for name in ('numpy','scipy','pyserial','fastapi','uvicorn','python-multipart','soundfile','funasr','torch','aec-audio-processing','silero-vad','onnxruntime','sherpa-onnx'):
        try: packages[name] = version(name)
        except PackageNotFoundError: missing.append(name)
    key = Path(os.environ.get('FFV_PAIRING_KEY_FILE', str(ROOT/'media-work/wifi-pairing.key')))
    model = Path(os.environ.get('FFV_ASR_MODEL', str(ROOT/'models/SenseVoiceSmall')))
    wake = Path(os.environ.get('FFV_WAKE_MODEL', str(ROOT/'models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20')))
    ffmpeg = os.environ.get('FFV_FFMPEG') or shutil.which('ffmpeg')
    ffprobe = os.environ.get('FFV_FFPROBE') or shutil.which('ffprobe')
    asr_files = ('model.pt', 'config.yaml', 'am.mvn')
    wake_files = ('tokens.txt','encoder-epoch-13-avg-2-chunk-16-left-64.int8.onnx','decoder-epoch-13-avg-2-chunk-16-left-64.onnx','joiner-epoch-13-avg-2-chunk-16-left-64.int8.onnx')
    assets = {'asr_model_files':all((model/name).is_file() for name in asr_files), 'wake_model_files':all((wake/name).is_file() for name in wake_files), 'pairing_key_32_bytes':key.is_file() and key.stat().st_size==32, 'ffmpeg':bool(ffmpeg and (Path(ffmpeg).is_file() or shutil.which(ffmpeg))), 'ffprobe':bool(ffprobe and (Path(ffprobe).is_file() or shutil.which(ffprobe)))}
    return {'ready':not missing and all(assets.values()), 'packages':packages, 'missing_packages':missing, 'assets':assets, 'devices_started':False}

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check',action='store_true')
    args = parser.parse_args()
    if args.check:
        result = check()
        print(json.dumps(result,ensure_ascii=False))
        raise SystemExit(0 if result['ready'] else 1)
    os.chdir(ROOT)
    runpy.run_path(str(ROOT/'app.py'),run_name='__main__')
