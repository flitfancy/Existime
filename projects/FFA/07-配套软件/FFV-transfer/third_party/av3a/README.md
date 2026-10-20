# Local AV3A decoding backend

`media_decode.py` automatically uses this backend for an AV3A audio track, including
one unwrapped from NCM. AAC/M4A, FLAC and other ordinary codecs continue to use the
existing `S:\Fireflys\FFmpeg` installation. No system FFmpeg replacement is needed.

## Tested Windows binary

- Release: https://github.com/lioumin1/Sourcecodeforplayer/releases/tag/ffmpeg-n6.1-win64-static
- Asset: `ffmpeg--win64-static.7z`
- Native audio decoder: `libarcdav3a`
- Archive SHA256: `81D153911CEE38013B33A2EF7E389D02A1AFE5F5188860F873A0D25BC46850E5`
- `ffmpeg.exe` SHA256: `B3817C75B74587C9DA3EE331B4DDF3309FAFE0647BFD792491B957DE90764070`

To provision another computer, download the asset from that release, verify its
archive checksum, extract it into a separate directory, and copy only `ffmpeg.exe`
and its license notice here. Verify the executable checksum above. These hashes
were calculated locally from the tested release, not independently signed upstream.
Restart the audio service after provisioning if a prior job reported it missing.

The executable is installed locally and excluded from Git. The archive includes
COPYING.GPLv3.txt, retained here; verify the embedded AV3A library's own distribution
terms before including this binary in a public application package.

## Runtime contract

The stable ffprobe detects AV3A from its `av3a` track tag. The native build's stdout
contains diagnostic text, including in JSON/quiet modes, so it is not used as a
probe or PCM pipe. Audio is written as PCM16 mono into a unique temporary directory
under the current decode job, at the requested supported sample rate.

The child process supports cancellation and a 600-second timeout. It is terminated
and reaped before partial files are removed; successful temporary files are removed
after their audio bytes have been read. Existing duration, size and PCM validation
still applies, and the scheduler receives the same PCMSource as for ordinary files.

Validation: `python -m unittest test_media_decode test_media_cancel test_output_pipeline`.
Native local-file verification and benchmark results are in
`media-work/av3a-native-test/` (private, ignored runtime artifacts).
