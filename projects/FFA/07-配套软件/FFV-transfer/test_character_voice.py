"""TTS process boundary and output contract without loading GPU models."""
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import wave

from character_voice import CharacterVoice


class CharacterVoiceTests(unittest.TestCase):
    def setUp(self):
        self.voice = CharacterVoice()
        self.addCleanup(self.voice.close)
        self.voice.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.voice.temp.name)
        self.process = Mock()
        self.process.poll.return_value = None
        self.voice.process = self.process
        self.voice._start = Mock()

    def reply(self, message):
        job = json.loads(message)
        folder = self.root / job['id']
        with wave.open(str(folder / 'speech.wav'), 'wb') as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(32000)
            wav.writeframes(b'\x00\x10' * 3200)
        (folder / 'result.json').write_text('{"ok":true}', encoding='utf-8')

    def test_wav_and_pcm_are_48k_mono_and_job_files_are_removed(self):
        self.process.stdin.write.side_effect = self.reply
        for raw in (False, True):
            audio = self.voice.synthesize('你好', raw, {'engine': 'gpt-sovits'})
            if raw:
                self.assertEqual(len(audio), 4800 * 2)
            else:
                with wave.open(io.BytesIO(audio), 'rb') as wav:
                    self.assertEqual((wav.getframerate(), wav.getnchannels(), wav.getsampwidth()), (48000, 1, 2))
                    self.assertEqual(wav.getnframes(), 4800)
            self.assertEqual(list(self.root.iterdir()), [])
        self.process.terminate.assert_not_called()

    def test_timeout_reaps_worker_before_removing_partial_audio(self):
        self.process.stdin.write.side_effect = lambda text: (self.root / json.loads(text)['id'] / 'speech.wav').write_bytes(b'partial')
        observed = []
        self.process.wait.side_effect = lambda **kw: observed.append(self.root.exists())
        with patch.object(self.voice, '_wait', side_effect=TimeoutError('timeout')):
            with self.assertRaises(TimeoutError):
                self.voice.synthesize('你好', config={'engine': 'gpt-sovits'})
        self.process.terminate.assert_called_once()
        self.assertEqual(observed, [True])
        self.assertFalse(self.root.exists())

    def test_reported_worker_failure_is_not_returned_as_audio(self):
        def failure(message):
            folder = self.root / json.loads(message)['id']
            (folder / 'result.json').write_text('{"ok":false,"error":"model failed"}', encoding='utf-8')
        self.process.stdin.write.side_effect = failure
        with self.assertRaisesRegex(RuntimeError, 'model failed'):
            self.voice.synthesize('你好', config={'engine': 'gpt-sovits'})
        self.process.terminate.assert_called_once()
        self.assertFalse(self.root.exists())


if __name__ == '__main__':
    unittest.main()
