"""Run with AstrBot's bundled Python; no server startup or QQ messages."""
import asyncio
from pathlib import Path
import sys
import unittest
from unittest.mock import AsyncMock, Mock

import os
if os.environ.get('ASTRBOT_APP_ROOT'):
    sys.path.insert(0, os.environ['ASTRBOT_APP_ROOT'])
sys.path.insert(0, str(Path(__file__).parent))
from astrbot.api.event import MessageChain
from astrbot.api.message_components import Plain, Record
from astrbot_plugin_firefly_voice.main import FireflyVoice, LocalVoiceEvent, WebMessageEvent, OwnerVoice, owner_session, spoken_text
from astrbot_plugin_firefly_voice.local_provider_policy import LocalProviderPolicy

CFG = {'admins_id': ['100001'], 'platform': [{'id': 'qq-test', 'type': 'aiocqhttp', 'enable': True}]}


class BridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_retry_policy_waits_for_provider_loading_and_refreshes(self):
        from types import SimpleNamespace
        plugin = object.__new__(FireflyVoice)
        plugin.provider_policy = LocalProviderPolicy()
        plugin.context = Mock()
        plugin.context.get_all_providers.return_value = []
        await plugin.configure_local_provider_retries()
        client = SimpleNamespace(base_url='http://127.0.0.1:11434/v1', max_retries=2)
        plugin.context.get_all_providers.return_value = [SimpleNamespace(client=client)]
        await plugin.refresh_local_provider_retries(None, None)
        self.assertEqual(client.max_retries, 0)
        plugin.provider_policy.restore()
        self.assertEqual(client.max_retries, 2)

    def test_spoken_text_asides_and_dialogue(self):
        for text, expected in [
            ('（轻轻笑）晚上好。(停顿)今天好吗？', '晚上好。今天好吗？'),
            ('（抬头（微笑）\n看你）“你好。”', '“你好。”'),
            ('你好。（轻声说', '你好。'),
            ('（微笑）……', ''),
            ('你好，今天怎么样？', '你好，今天怎么样？'),
        ]:
            with self.subTest(text=text):
                self.assertEqual(spoken_text(text), expected)

    async def test_both_tts_formats_receive_only_dialogue(self):
        plugin = object.__new__(FireflyVoice)
        response = AsyncMock()
        response.raise_for_status = Mock()
        response.read.return_value = b'audio'
        manager = AsyncMock()
        manager.__aenter__.return_value = response
        plugin.http = Mock()
        plugin.http.post.return_value = manager
        original = '（' + '旁白' * 800 + '）你好。'
        for fmt in ('pcm', 'wav'):
            self.assertEqual(await plugin.speech(original, fmt), b'audio')
            self.assertEqual(plugin.http.post.call_args.kwargs['json'],
                             {'input': '你好。', 'response_format': fmt})

    async def test_aside_only_local_reply_completes_without_audio(self):
        plugin = object.__new__(FireflyVoice)
        plugin.reply_lock = asyncio.Lock()
        plugin.report = AsyncMock()
        plugin.http = Mock()
        await plugin.local_reply('test', '（轻轻微笑）')
        plugin.http.post.assert_not_called()
        plugin.report.assert_awaited_with('complete', session='test', text='（轻轻微笑）')

    async def test_aside_only_qq_keeps_original_text_without_audio(self):
        from astrbot.api.event import AstrMessageEvent, ResultContentType
        base = self.event()
        event = AstrMessageEvent('你好', base.message_obj, base.platform_meta, '100001')
        event.set_extra('firefly_voice_reply', True)
        result = event.plain_result('（轻轻微笑）')
        result.set_result_content_type(ResultContentType.LLM_RESULT)
        event.set_result(result)
        plugin = object.__new__(FireflyVoice)
        plugin.http = Mock()
        await plugin.qq_voice_reply(event)
        plugin.http.post.assert_not_called()
        self.assertEqual(len(result.chain), 1)
        self.assertEqual(result.chain[0].text, '（轻轻微笑）')

    def event(self):
        bridge = AsyncMock()
        return LocalVoiceEvent({'session': 'turn-test', 'text': '测试'}, '100001', 'qq-test', bridge)

    async def test_shared_context_but_local_reply_route(self):
        event = self.event()
        self.assertEqual(event.unified_msg_origin, 'qq-test:FriendMessage:100001')
        self.assertEqual(event.get_platform_name(), 'firefly_voice')
        self.assertFalse(event.get_extra('_api_key_allow_admin_role'))
        await event.send(MessageChain([Plain('这是回复')]))
        event.bridge.local_reply.assert_awaited_once_with('turn-test', '这是回复')

    async def test_qq_voice_filter_survives_stt_and_excludes_other_senders(self):
        event = self.event()
        # Use the base event, because LocalVoiceEvent intentionally always matches.
        from astrbot.api.event import AstrMessageEvent
        event = AstrMessageEvent('识别完成', event.message_obj, event.platform_meta, '100001')
        event.platform_meta.name = 'aiocqhttp'; event.platform_meta.id = 'qq-test'
        event.message_obj.raw_message = {'message': [{'type': 'record', 'data': {'file': 'a.wav'}}]}
        self.assertTrue(OwnerVoice().filter(event, CFG))
        event.message_obj.sender.user_id = '999999'
        self.assertFalse(OwnerVoice().filter(event, CFG))
        event.message_obj.sender.user_id = '100001'
        event.message_obj.raw_message = {'message': [{'type': 'text', 'data': {'text': '你好'}}]}
        self.assertFalse(OwnerVoice().filter(event, CFG))

    async def test_multiple_owners_refuse_ambiguous_binding(self):
        with self.assertRaises(ValueError):
            owner_session(dict(CFG, admins_id=['100001', '100002']))

    async def test_web_reply_never_plays_board_or_calls_qq(self):
        event = WebMessageEvent({'session': 'web-test', 'text': '网页测试'}, '100001', 'qq-test', AsyncMock())
        await event.send(MessageChain([Plain('网页回复')]))
        self.assertEqual(event.parts, ['网页回复'])
        event.bridge.local_reply.assert_not_called()
        self.assertEqual(event.unified_msg_origin, 'qq-test:FriendMessage:100001')
        self.assertEqual(event.get_platform_name(), 'firefly_web')

    async def test_qq_adapter_can_be_disabled_without_breaking_web(self):
        config = dict(CFG, platform=[dict(CFG['platform'][0], enable=False)])
        self.assertEqual(owner_session(config), ('100001', 'qq-test'))

    async def test_qq_synthesis_failure_preserves_text(self):
        import aiohttp
        event = self.event()
        from astrbot.api.event import AstrMessageEvent
        event = AstrMessageEvent('你好', event.message_obj, event.platform_meta, '100001')
        event.set_extra('firefly_voice_reply', True)
        result = event.plain_result('文字保留')
        result.set_result_content_type(__import__('astrbot.api.event', fromlist=['ResultContentType']).ResultContentType.LLM_RESULT)
        event.set_result(result)
        plugin = object.__new__(FireflyVoice)
        plugin.speech = AsyncMock(side_effect=aiohttp.ClientError())
        await plugin.qq_voice_reply(event)
        self.assertEqual(result.chain[0].text, '文字保留')
        self.assertFalse(any(isinstance(comp, Record) for comp in result.chain))


if __name__ == '__main__':
    unittest.main()
