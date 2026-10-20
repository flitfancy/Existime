import io
import queue
import threading
import unittest
import wave
from unittest.mock import patch

from fastapi import FastAPI
from fastapi.testclient import TestClient
from speech_services import register_speech_routes, synthesize_voice
from voice_events import VoiceEvents


class SpeechServiceTests(unittest.TestCase):
    def setUp(self):
        self.jobs = queue.Queue(8)
        self.state = {'model': 'ready'}
        self.app = FastAPI()
        register_speech_routes(self.app, self.jobs, self.state, VoiceEvents())
        self.client = TestClient(self.app)

    def test_file_recognition_uses_existing_queue_without_capture_events(self):
        body = io.BytesIO()
        with wave.open(body, 'wb') as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(16000)
            wav.writeframes(b'\x01\x00' * 16000)
        observed = []
        def worker():
            job = self.jobs.get(timeout=10)
            observed.append((job.rate, len(job.audio)))
            job.future.set_result('你好。')
            self.jobs.task_done()
        thread = threading.Thread(target=worker)
        thread.start()
        response = self.client.post('/v1/audio/transcriptions', files={'file': ('test.wav', body.getvalue())})
        thread.join(10)
        self.assertEqual(response.status_code, 200, response.text)
        self.assertEqual(response.json(), {'text': '你好。'})
        self.assertEqual(observed, [(16000, 16000)])

    def test_recognition_not_ready_and_invalid_file(self):
        self.state['model'] = 'loading'
        self.assertEqual(self.client.post('/v1/audio/transcriptions').status_code, 503)
        self.state['model'] = 'ready'
        self.assertEqual(self.client.post('/v1/audio/transcriptions', files={'file': ('bad.wav', b'bad')}).status_code, 400)
        self.assertTrue(self.jobs.empty())

    def test_tts_validation_and_pcm_contract(self):
        for payload in [{}, {'input': 'x' * 1501}, {'input': 'ok', 'response_format': 'mp3'}, {'input': 7}]:
            self.assertEqual(self.client.post('/v1/audio/speech', json=payload).status_code, 400)
        with patch('speech_services.synthesize_voice', return_value=(b'\x00\x00' * 4800, 'gpt-sovits')) as synth:
            response = self.client.post('/v1/audio/speech', json={'input': '你好', 'response_format': 'pcm'})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.headers['X-Audio-Sample-Rate'], '48000')
        self.assertEqual(response.headers['X-Voice-Engine'], 'gpt-sovits')
        self.assertEqual(self.state['tts']['phase'], 'ready')
        synth.assert_called_once_with('你好', True)

    def test_character_failure_does_not_silently_switch_voices(self):
        with patch('speech_services.load_config', return_value={'engine': 'gpt-sovits'}), \
                patch('speech_services.character_voice.synthesize', side_effect=RuntimeError('model failed')), \
                patch('speech_services.synthesize_temporary') as temporary:
            response = self.client.post('/v1/audio/speech', json={'input': '你好'})
        self.assertEqual(response.status_code, 502)
        self.assertEqual(self.state['tts']['phase'], 'error')
        temporary.assert_not_called()

    def test_explicit_windows_engine_remains_available(self):
        with patch('speech_services.load_config', return_value={'engine': 'windows-temporary'}), \
                patch('speech_services.synthesize_temporary', return_value=b'wav'):
            self.assertEqual(synthesize_voice('你好'), (b'wav', 'windows-temporary'))

    def test_bridge_heartbeat_preserves_reply_and_validates_input(self):
        self.assertFalse(self.client.get('/v1/voice/bridge').json()['connected'])
        self.client.post('/v1/voice/bridge', json={'phase': 'complete', 'session': 's', 'text': '回复'})
        self.client.post('/v1/voice/bridge', json={'phase': 'heartbeat'})
        result = self.client.get('/v1/voice/bridge').json()
        self.assertTrue(result['connected']); self.assertEqual(result['text'], '回复')
        self.assertEqual(self.client.post('/v1/voice/bridge', json={'phase': 'arbitrary'}).status_code, 400)


if __name__ == '__main__':
    unittest.main()
