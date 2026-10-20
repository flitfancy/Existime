"""Private stdin worker, run with GPT-SoVITS's own Python environment."""
import argparse
import json
import os
from pathlib import Path
import sys
import traceback


def publish(path, data):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
    temp.replace(path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', required=True)
    parser.add_argument('--run-dir', required=True)
    args = parser.parse_args()
    run_dir = Path(args.run_dir).resolve()
    config = json.loads(Path(args.config).read_text(encoding='utf-8'))
    root = Path(config['project_root']).resolve()
    os.chdir(root)
    sys.path.insert(0, str(root))
    sys.path.insert(0, str(root / 'GPT_SoVITS'))
    try:
        import numpy as np
        import soundfile as sf
        from GPT_SoVITS.TTS_infer_pack.TTS import TTS, TTS_Config

        infer = TTS_Config({'custom': config['inference']})
        # Upstream saves weight changes; keep these writes out of the user's WebUI config.
        infer.configs_path = str(run_dir / 'infer.yaml')
        pipeline = TTS(infer)
        publish(run_dir / 'ready.json', {'ok': True})
    except Exception:
        traceback.print_exc()
        publish(run_dir / 'ready.json', {'ok': False, 'error': 'GPT-SoVITS 模型加载失败，请查看本机 tts-worker.log'})
        return

    # EOF on this private pipe also exits the worker after a parent-service restart.
    for line in sys.stdin:
        job = json.loads(line)
        job_id = job['id']
        if len(job_id) != 32 or any(c not in '0123456789abcdef' for c in job_id):
            raise ValueError('Invalid internal job id')
        folder = run_dir / job_id
        try:
            inputs = dict(config['synthesis'], text=job['text'], media_type='wav',
                          return_fragment=False, streaming_mode=False)
            audio_parts = []
            rate = None
            frames = 0
            # Fully exhaust the generator: upstream can yield silence before raising.
            for current_rate, audio in pipeline.run(inputs):
                if rate is not None and current_rate != rate:
                    raise ValueError('Synthesis sample rate changed')
                rate = current_rate
                frames += len(audio)
                if frames > rate * 600:
                    raise ValueError('Speech exceeds ten minutes')
                audio_parts.append(audio)
            if not audio_parts:
                raise ValueError('Empty synthesis')
            audio = np.concatenate(audio_parts)
            if not len(audio) or not np.isfinite(audio).all() or not np.any(audio):
                raise ValueError('Invalid or silent synthesis')
            sf.write(str(folder / 'speech.wav'), audio, rate, subtype='PCM_16')
            publish(folder / 'result.json', {'ok': True, 'sample_rate': rate, 'frames': len(audio)})
        except Exception:
            traceback.print_exc()
            publish(folder / 'result.json', {'ok': False, 'error': 'GPT-SoVITS 合成失败，请查看本机 tts-worker.log'})


if __name__ == '__main__':
    main()
