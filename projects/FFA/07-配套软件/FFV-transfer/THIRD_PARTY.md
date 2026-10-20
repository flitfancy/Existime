# Third-party components

## WebRTC AEC Python adapter

- Distribution: https://pypi.org/project/aec-audio-processing/1.0.1/ (BSD-3-Clause).
- Windows CPython 3.11 wheel pinned by SHA256 in `requirements-aec.txt`.
- Source distribution SHA256: `f03a3ce3f45bf15a04a8504d0f692a651180ca2d3171bd9f3de0516e5f2b8360`.
- Reviewed source `src/audio_processing_module.cpp`: constructs WebRTC AudioProcessing,
  enables the non-mobile echo canceller; processing is frame-based and local.
- `aec_processor.py` explicitly disables noise suppression, AGC and wrapper VAD;
  one native processor per microphone, mono reverse stream, 16 kHz / 10 ms PCM16.
- The wrapper's advertised VAD is not used; the inspected source leaves its VAD
  initialization commented out. Existing Silero remains the project's VAD.
- Installed distribution license preserved in `third_party/aec_audio_processing/`.
- `aec_runtime.py` now uses this adapter in the persistent audio worker before
  production VAD/STT. Diagnostics exercise that same runtime. Raw microphone
  subscription and original recordings remain available for comparison.

## Silero VAD / ONNX Runtime

- Model: Silero VAD 6.2.1, https://github.com/snakers4/silero-vad (MIT).
- Runtime: ONNX Runtime 1.29.0, https://github.com/microsoft/onnxruntime (MIT).
- Versions: `requirements-vad.txt`; installed in the existing local audio Python environment.
- `vad_observer.py` loads the package's bundled `silero_vad/data/silero_vad.onnx`
  (2,327,524 bytes) directly through ONNX Runtime CPUExecutionProvider.
- Two independent channel states, 16 kHz input, 512-sample windows and 64-sample context.
- Model inference is local and offline; no GPU provider, downloads or uploads during inference.
- Existing numpy/scipy perform streaming resampling. Install pinned additions with
  `python -m pip install --only-binary=:all: --no-deps -r requirements-vad.txt`
  in the existing FFV environment; this is not a standalone service dependency manifest.

## pyNCMDUMP

- Upstream: https://github.com/allenfrostline/pyNCMDUMP
- Pinned commit: `2217cbddd4e0b52e107d8ade6e9e82942423e87c`
- License: MIT, preserved in `third_party/pyNCMDUMP/LICENSE.txt`
- Use in this project: locally unwrap an NCM container to its embedded audio before FFmpeg decoding. Output discovery shares the upload format list, including M4A (verified with a local NCM file), MP3 and FLAC.
- Network behavior in the reviewed converter path: none
- AV3A tracks use the optional native backend below; ordinary AAC M4A uses the existing FFmpeg.

## Native AV3A / Audio Vivid

- The tested Windows FFmpeg build embeds `libarcdav3a` and is installed at `third_party/av3a/ffmpeg.exe`.
- Release source, archive/executable SHA256, provisioning and distribution notes: [third_party/av3a/README.md](third_party/av3a/README.md).
- Native output is a private PCM file, never stdout (which contains library diagnostics). No new service, port or device firmware is required.
- Executable files are local dependencies excluded from Git; the release's GPL notice is preserved alongside the README.

Only use this adapter with local media the user is authorized to play.
