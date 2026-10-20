import time
import unittest
import uuid
from fastapi import FastAPI
from fastapi.testclient import TestClient
from dialogue_service import register_dialogue_routes
from voice_events import VoiceEvents


class DialogueTests(unittest.TestCase):
    def setUp(self):
        self.state = {'dialogue': {'last_seen': time.time(), 'protocol': 2}}
        self.events = VoiceEvents()
        app = FastAPI()
        register_dialogue_routes(app, self.state, self.events)
        self.client = TestClient(app)

    def submit(self, text='网页测试', key=None):
        return self.client.post('/v1/dialogue/messages', json={'text': text, 'request_id': key or str(uuid.uuid4())})

    def test_retry_is_idempotent_and_mismatch_rejected(self):
        key = str(uuid.uuid4()); cursor = self.events.cursor()
        self.assertEqual(self.submit(key=key).status_code, 202)
        self.assertEqual(self.submit(key=key).status_code, 200)
        self.assertEqual(self.submit('不同内容', key).status_code, 409)
        batch, _ = self.events.read_after(cursor)
        self.assertEqual(len(batch), 1)
        self.assertEqual(batch[0]['type'], 'dialogue.request')

    def test_offline_and_old_plugin_do_not_accept_messages(self):
        self.state['dialogue']['protocol'] = 1
        self.assertEqual(self.submit().status_code, 503)
        self.state['dialogue'].update(protocol=2, last_seen=0)
        self.assertEqual(self.submit().status_code, 503)

    def test_bounded_requests_release_on_completion(self):
        key = str(uuid.uuid4())
        self.submit(key=key);self.submit()
        self.assertEqual(self.submit().status_code, 429)
        response = self.client.post('/v1/dialogue/result', json={'session': key, 'phase': 'complete', 'reply': '收到'})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(self.submit().status_code, 202)

    def test_snapshot_only_accepts_displayable_roles(self):
        response = self.client.post('/v1/dialogue/snapshot', json={'revision': 'r', 'messages': [{'role': 'system', 'content': 'secret'}]})
        self.assertEqual(response.status_code, 400)
        self.client.post('/v1/dialogue/snapshot', json={'revision': 'r', 'messages': [{'role': 'user', 'content': '<script>text</script>'}]})
        self.assertEqual(self.client.get('/v1/dialogue').json()['messages'][0]['content'], '<script>text</script>')


if __name__ == '__main__':unittest.main()
