"""NCM output discovery at the converter boundary, without a board."""
from pathlib import Path
import subprocess
import json
import tempfile
import threading
import unittest
from unittest.mock import patch

from media_decode import decode_to_pcm, probe, unwrap_ncm


class NcmOutputTests(unittest.TestCase):
    def convert_outputs(self, folder, names):
        def convert(command, **kwargs):
            for name in names:
                (folder / name).write_bytes(b'audio')
            return subprocess.CompletedProcess(command, 0, b'', b'')
        return patch('media_decode._run', side_effect=convert)

    def test_embedded_formats_including_m4a_are_discovered(self):
        for suffix in ('.m4a', '.flac', '.mp3', '.wav', '.aac', '.ogg', '.opus', '.M4A'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as temp:
                folder = Path(temp)
                source = folder / 'input.ncm'
                source.write_bytes(b'ncm')
                names = ['input' + suffix, 'cover.jpg']
                with self.convert_outputs(folder, names):
                    self.assertEqual(unwrap_ncm(source, folder), folder / names[0])

    def test_existing_audio_and_directories_are_not_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / 'old.flac').write_bytes(b'old')
            def convert(command, **kwargs):
                (folder / 'directory.mp3').mkdir()
                return subprocess.CompletedProcess(command, 0, b'', b'')
            with patch('media_decode._run', side_effect=convert):
                with self.assertRaisesRegex(RuntimeError, '没有产生唯一音频文件'):
                    unwrap_ncm(folder / 'input.ncm', folder)

    def test_multiple_audio_outputs_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            with self.convert_outputs(folder, ['input.m4a', 'input.flac']):
                with self.assertRaisesRegex(RuntimeError, '没有产生唯一音频文件'):
                    unwrap_ncm(folder / 'input.ncm', folder)

    def test_av3a_is_identified_by_tag_without_native_probe(self):
        result = subprocess.CompletedProcess([], 0, json.dumps({
            'streams': [{'codec_tag_string': 'av3a', 'channels': 12, 'sample_rate': '44100'}],
            'format': {'duration': '225.3'},
        }).encode(), b'')
        with patch('media_decode._run', return_value=result):
            self.assertEqual(probe(Path('input.m4a'))['codec'], 'av3a')

    def test_normal_m4a_aac_is_not_rejected(self):
        result = subprocess.CompletedProcess([], 0, json.dumps({
            'streams': [{'codec_name': 'aac', 'codec_tag_string': 'mp4a',
                         'channels': 2, 'sample_rate': '44100'}],
            'format': {'duration': '225.3'},
        }).encode(), b'')
        with patch('media_decode._run', return_value=result):
            self.assertEqual(probe(Path('input.m4a'))['codec'], 'aac')


class NativeAv3aTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.binary = self.folder / 'decoder.exe'
        self.binary.touch()
        self.patch = patch('media_decode.AV3A_FFMPEG', self.binary)
        self.patch.start()
        self.addCleanup(self.patch.stop)
        self.metadata = {'codec': 'av3a', 'duration': .1}

    def test_native_stdout_does_not_enter_pcm_and_output_is_cleaned(self):
        pcm = b'\x01\x00' * 4800
        def run(command, **kwargs):
            Path(command[-1]).write_bytes(pcm)
            return subprocess.CompletedProcess(command, 0, b'Native debug log\n', b'')
        with patch('media_decode.probe', return_value=self.metadata), patch('media_decode._run', side_effect=run):
            result, meta = decode_to_pcm(self.folder / 'input.m4a', self.folder)
        self.assertEqual(result, pcm)
        self.assertEqual(meta['decoded_backend'], 'av3a-native')
        self.assertEqual(list(self.folder.glob('av3a-*')), [])

    def test_partial_output_is_removed_on_cancel_failure_and_timeout(self):
        for error in (InterruptedError('cancel'), subprocess.TimeoutExpired('decoder', 1),
                      subprocess.CalledProcessError(1, 'decoder', stderr=b'bad input')):
            with self.subTest(error=type(error).__name__):
                def run(command, **kwargs):
                    Path(command[-1]).write_bytes(b'partial')
                    raise error
                expected = InterruptedError if isinstance(error, InterruptedError) else RuntimeError
                with patch('media_decode.probe', return_value=self.metadata), patch('media_decode._run', side_effect=run):
                    with self.assertRaises(expected):
                        decode_to_pcm(self.folder / 'input.m4a', self.folder, cancelled=threading.Event())
                self.assertEqual(list(self.folder.glob('av3a-*')), [])

    def test_missing_decoder_has_actionable_error(self):
        self.binary.unlink()
        with patch('media_decode.probe', return_value=self.metadata):
            with self.assertRaisesRegex(RuntimeError, '尚未安装 AV3A'):
                decode_to_pcm(self.folder / 'input.m4a', self.folder)

    def test_normal_aac_keeps_existing_decoder(self):
        with patch('media_decode.probe', return_value={'codec': 'aac', 'duration': .1}), \
                patch('media_decode._decode_av3a') as native, \
                patch('media_decode._run', return_value=subprocess.CompletedProcess([], 0, b'\x01\x00', b'')):
            pcm, meta = decode_to_pcm(self.folder / 'input.m4a', self.folder)
        native.assert_not_called()
        self.assertEqual(pcm, b'\x01\x00')
        self.assertEqual(meta['decoded_backend'], 'ffmpeg')


if __name__ == '__main__':
    unittest.main()
