"""Exercise the real OpenAI SDK and AstrBot retry loop without network traffic."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
import httpx
from openai import AsyncOpenAI, APITimeoutError
import os
if os.environ.get('ASTRBOT_APP_ROOT'):
    sys.path.insert(0, os.environ['ASTRBOT_APP_ROOT'])
sys.path.insert(0, str(Path(__file__).parent))
from astrbot.core.provider.sources.request_retry import retry_provider_request
from astrbot_plugin_firefly_voice.local_provider_policy import LocalProviderPolicy


class RetryTests(unittest.IsolatedAsyncioTestCase):
    async def test_timeout_is_one_actual_attempt_across_both_layers(self):
        calls = []
        def fail(request):
            calls.append(request)
            raise httpx.ReadTimeout('test timeout', request=request)
        async with AsyncOpenAI(api_key='test', base_url='http://127.0.0.1:11434/v1',
                http_client=httpx.AsyncClient(transport=httpx.MockTransport(fail))) as client:
            policy = LocalProviderPolicy()
            self.assertEqual(policy.apply([SimpleNamespace(client=client)]), 1)
            with self.assertRaises(APITimeoutError):
                await retry_provider_request('test', lambda: client.chat.completions.create(
                    model='test', messages=[{'role': 'user', 'content': 'test'}]), max_attempts=1)
            self.assertEqual(len(calls), 1)
            policy.restore()
            self.assertEqual(client.max_retries, 2)

    async def test_cloud_other_ports_and_shared_client_unchanged(self):
        local = SimpleNamespace(base_url='http://localhost:11434/v1', max_retries=2)
        cloud = SimpleNamespace(base_url='https://example.test/v1', max_retries=2)
        other = SimpleNamespace(base_url='http://127.0.0.1:7865/v1', max_retries=2)
        policy = LocalProviderPolicy()
        providers = [SimpleNamespace(client=c) for c in (local, local, cloud, other)]
        self.assertEqual(policy.apply(providers), 1)
        self.assertEqual((local.max_retries, cloud.max_retries, other.max_retries), (0, 2, 2))
        policy.restore()
        self.assertEqual(local.max_retries, 2)


if __name__ == '__main__':
    unittest.main()
