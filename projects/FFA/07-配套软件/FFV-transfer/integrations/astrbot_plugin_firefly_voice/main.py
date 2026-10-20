"""Two physical reply routes, one existing AstrBot private conversation.

Uses AstrBot's event queue and normal pipeline, never calls an LLM directly.
No QQ send API is available to the local event's send implementation.
"""
import asyncio
import contextlib
import json
import hashlib
import re
from pathlib import Path
import tempfile
import time

import aiohttp
from .local_provider_policy import LocalProviderPolicy
from astrbot.api import logger
from astrbot.api.event import AstrMessageEvent, filter
from astrbot.api.message_components import Plain, Record
from astrbot.api.star import Star, register
from astrbot.core.platform.astrbot_message import AstrBotMessage, MessageMember
from astrbot.core.platform.message_type import MessageType
from astrbot.core.platform.platform_metadata import PlatformMetadata
from astrbot.core.utils.astrbot_path import get_astrbot_temp_path

BASE = 'http://127.0.0.1:7865'


def spoken_text(text):
    """Remove round-bracket asides only from speech, including nested asides.

    An unfinished opening bracket hides the remaining aside. Mixed Chinese and
    ASCII round brackets are accepted; quotation marks remain spoken dialogue.
    """
    depth = 0
    output = []
    for char in text:
        if char in '(（':
            depth += 1
        elif char in ')）':
            depth = max(0, depth - 1)
        elif depth == 0:
            output.append(char)
    result = re.sub(r'\s+', ' ', ''.join(output)).strip()
    return result if any(char.isalnum() for char in result) else ''


def owner_session(config):
    owners = [str(value) for value in config.get('admins_id', []) if str(value).isdigit()]
    platforms = [p for p in config.get('platform', []) if p.get('type') == 'aiocqhttp']
    if len(owners) != 1 or len(platforms) != 1:
        raise ValueError('需要唯一的 QQ 管理员与启用的 QQ 平台才能自动绑定私聊会话')
    return owners[0], platforms[0]['id']


def incoming_voice(event):
    if any(isinstance(comp, Record) for comp in event.get_messages()):
        return True
    raw = event.message_obj.raw_message
    message = raw.get('message', []) if hasattr(raw, 'get') else []
    return isinstance(message, list) and any(isinstance(comp, dict) and comp.get('type') == 'record' for comp in message)


class OwnerVoice(filter.CustomFilter):
    def filter(self, event, cfg):
        if isinstance(event, LocalVoiceEvent):
            return True
        try:
            owner, platform = owner_session(cfg)
        except ValueError:
            return False
        return (event.get_platform_name() == 'aiocqhttp' and event.get_platform_id() == platform
                and event.is_private_chat() and event.get_sender_id() == owner and incoming_voice(event))


class LocalVoiceEvent(AstrMessageEvent):
    def __init__(self, turn, owner, qq_platform, bridge):
        message = AstrBotMessage()
        message.type = MessageType.FRIEND_MESSAGE
        message.self_id = 'firefly-voice'
        message.session_id = owner
        message.sender = MessageMember(owner, '本地语音')
        message.message_id = 'ffv-' + turn['session']
        message.message_str = turn['text']
        message.message = [Plain(turn['text'])]
        message.raw_message = {'source': 'firefly-voice', 'turn': turn['session']}
        super().__init__(turn['text'], message, PlatformMetadata(
            name='firefly_voice', description='板子本地语音', id='firefly-voice',
            support_streaming_message=False, support_proactive_message=False), owner)
        # Same UMO uses AstrBot's existing selected conversation, persona overrides,
        # model selection and per-session LLM lock. Physical reply still uses send().
        self.unified_msg_origin = f'{qq_platform}:FriendMessage:{owner}'
        self.bridge = bridge
        self.turn_id = turn['session']
        self.set_extra('enable_streaming', False)
        self.set_extra('_api_key_allow_admin_role', False)

    def cleanup_temporary_local_files(self):
        super().cleanup_temporary_local_files()
        self.bridge.schedule_history()

    async def send(self, message):
        if not message:
            return
        text = ''.join(comp.text for comp in message.chain if isinstance(comp, Plain)).strip()
        if text:
            await self.bridge.local_reply(self.turn_id, text)
        self._has_send_oper = True

    async def send_streaming(self, generator, use_fallback=False):
        # Defensive fallback if another plugin explicitly enables streaming.
        from astrbot.api.event import MessageChain
        chain = []
        async for item in generator:
            chain.extend(item.chain)
        await self.send(MessageChain(chain))


class WebMessageEvent(LocalVoiceEvent):
    def __init__(self, turn, owner, qq_platform, bridge):
        super().__init__(turn, owner, qq_platform, bridge)
        self.platform_meta.name = 'firefly_web'
        self.platform_meta.id = 'firefly-web'
        self.message_obj.sender.nickname = '网页对话'
        self.message_obj.raw_message['source'] = 'firefly-web'
        self.parts = []

    async def send(self, message):
        if message:
            text = ''.join(comp.text for comp in message.chain if isinstance(comp, Plain)).strip()
            if text:self.parts.append(text)
        self._has_send_oper = True

    def cleanup_temporary_local_files(self):
        super().cleanup_temporary_local_files()
        self.bridge.background(self.bridge.finish_web(self.turn_id, '\n'.join(self.parts)))


@register('astrbot_plugin_firefly_voice', 'Yunwe', '网页、QQ 与板子共用会话的本地语音入口', '0.2.2')
class FireflyVoice(Star):
    def __init__(self, context):
        super().__init__(context)
        self.http = None
        self.tasks = []
        self.cursor = None
        self.reply_lock = asyncio.Lock()
        self.background_tasks = set()
        self.history_lock = asyncio.Lock()
        self.provider_policy = LocalProviderPolicy()

    async def initialize(self):
        self.owner, self.qq_platform = owner_session(self.context.get_config())
        self.umo = f'{self.qq_platform}:FriendMessage:{self.owner}'
        self.http = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=180),
                                          json_serialize=lambda data: json.dumps(data, ensure_ascii=False))
        self.tasks = [asyncio.create_task(self.listen()), asyncio.create_task(self.heartbeat())]

    @filter.on_astrbot_loaded()
    async def configure_local_provider_retries(self):
        # Providers load after plugin.initialize during desktop startup.
        count = self.provider_policy.apply(self.context.get_all_providers())
        logger.info('[Firefly Voice] 本地 Ollama SDK 重试已关闭：%d 个客户端', count)

    @filter.on_llm_request()
    async def refresh_local_provider_retries(self, event, request):
        # Provider settings can recreate SDK clients without reloading this plugin.
        self.provider_policy.apply(self.context.get_all_providers())

    async def terminate(self):
        self.tasks.extend(self.background_tasks)
        for task in self.tasks:
            task.cancel()
        for task in self.tasks:
            with contextlib.suppress(asyncio.CancelledError):
                await task
        if self.http:
            await self.http.close()
        self.provider_policy.restore()

    def background(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.background_tasks.add(task)
        task.add_done_callback(self.background_tasks.discard)

    def schedule_history(self):
        self.background(self.sync_history())

    async def sync_history(self):
        async with self.history_lock:
            await self._sync_history()

    async def _sync_history(self):
        try:
            manager = self.context.conversation_manager
            cid = await manager.get_curr_conversation_id(self.umo)
            if not cid:return
            conversation = await manager.get_conversation(self.umo, cid)
            history = json.loads(conversation.history or '[]')
            messages = []
            for row in history:
                if row.get('role') not in ('user', 'assistant'):continue
                content = row.get('content', '')
                if isinstance(content, list):
                    content = '\n'.join(str(part.get('text', '')) for part in content
                                        if isinstance(part, dict) and part.get('type') == 'text'
                                        and not str(part.get('text', '')).startswith('<system_reminder>'))
                if isinstance(content, str) and content.strip():
                    messages.append({'role': row['role'], 'content': content[:6000]})
            messages = messages[-40:]
            revision = hashlib.sha256(json.dumps([cid, messages], ensure_ascii=False).encode()).hexdigest()
            async with self.http.post(BASE + '/v1/dialogue/snapshot', json={'revision': revision, 'messages': messages}) as response:
                response.raise_for_status();await response.read()
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError, AttributeError):
            logger.warning('[Firefly Voice] 对话展示快照暂未同步')

    async def finish_web(self, session, text):
        try:
            async with self.http.post(BASE + '/v1/dialogue/result', json={
                'session': session, 'phase': 'complete' if text else 'error',
                'reply': text[:6000], 'error': '' if text else '本轮没有文字回复，请查看 AstrBot 状态',
            }) as response:
                response.raise_for_status();await response.read()
            await self.sync_history()
        except (aiohttp.ClientError, asyncio.TimeoutError):
            logger.warning('[Firefly Voice] 网页回复状态暂未送达；历史仍保存在 AstrBot')

    async def report(self, phase, **fields):
        async with self.http.post(BASE + '/v1/voice/bridge', json=dict(phase=phase, **fields)) as response:
            response.raise_for_status()
            await response.read()

    async def heartbeat(self):
        while True:
            try:
                await self.report('heartbeat', protocol=2)
                await self.sync_history()
            except (aiohttp.ClientError, asyncio.TimeoutError):
                pass
            await asyncio.sleep(10)

    async def listen(self):
        while True:
            try:
                headers = {'Last-Event-ID': self.cursor} if self.cursor else {}
                timeout = aiohttp.ClientTimeout(total=None, connect=10, sock_read=25)
                async with self.http.get(BASE + '/v1/voice/events', headers=headers, timeout=timeout) as response:
                    response.raise_for_status()
                    await self.report('ready')
                    async for raw in response.content:
                        if not raw.startswith(b'data: '):
                            continue
                        event = json.loads(raw[6:])
                        self.cursor = event['id']
                        if event['type'] not in ('voice.turn.completed', 'dialogue.request'):
                            continue
                        turn = event['data']
                        is_web = event['type'] == 'dialogue.request'
                        if (not is_web and turn.get('reason') != 'utterance') or not turn.get('text', '').strip():
                            continue
                        if time.time() - event['time'] > 30:
                            if is_web:
                                await self.finish_web(turn['session'], '')
                            else:
                                await self.report('error', session=turn['session'], error='指令已过期，请重新说一遍')
                            continue
                        if is_web:
                            async with self.http.post(BASE + '/v1/dialogue/result', json={'session': turn['session'], 'phase': 'thinking'}) as ack:
                                ack.raise_for_status();await ack.read()
                            message = WebMessageEvent(turn, self.owner, self.qq_platform, self)
                        else:
                            await self.report('thinking', session=turn['session'])
                            message = LocalVoiceEvent(turn, self.owner, self.qq_platform, self)
                        await self.context.get_event_queue().put(message)
            except (aiohttp.ClientError, asyncio.TimeoutError, ValueError, KeyError):
                await asyncio.sleep(2)

    @filter.custom_filter(OwnerVoice, raise_error=False)
    async def prepare_voice(self, event):
        # Runs after AstrBot's existing STT preprocessor and before the LLM stage.
        event.set_extra('enable_streaming', False)
        event.set_extra('firefly_voice_reply', True)

    async def speech(self, text, fmt):
        text = spoken_text(text)
        if not text:
            return None
        if len(text) > 1500:
            raise ValueError('回复较长，已保留文字，本轮不自动朗读')
        async with self.http.post(BASE + '/v1/audio/speech', json={
            'input': text, 'response_format': fmt,
        }) as response:
            response.raise_for_status()
            return await response.read()

    async def local_reply(self, session, text):
        async with self.reply_lock:
            await self.report('text', session=session, text=text[:6000])
            try:
                pcm = await self.speech(text, 'pcm')
                if pcm is None:
                    await self.report('complete', session=session, text=text[:6000])
                    return
                await self.report('playing', session=session, text=text)
                async with self.http.post(BASE + '/v1/audio/output?rate=48000', data=pcm,
                        timeout=aiohttp.ClientTimeout(total=660)) as response:
                    if response.status == 409:
                        raise ValueError('扬声器正在播放音乐，本轮回复已保留为文字')
                    response.raise_for_status()
                    await response.read()
                await self.report('complete', session=session, text=text)
            except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as exc:
                await self.report('error', session=session, text=text[:6000], error=str(exc)[:240])

    @filter.on_decorating_result()
    async def qq_voice_reply(self, event):
        if isinstance(event, LocalVoiceEvent) or not event.get_extra('firefly_voice_reply'):
            return
        result = event.get_result()
        if not result or not result.is_llm_result() or any(isinstance(comp, Record) for comp in result.chain):
            return
        text = ''.join(comp.text for comp in result.chain if isinstance(comp, Plain)).strip()
        if not text:
            return
        try:
            audio = await self.speech(text, 'wav')
            if audio is None:
                return
            with tempfile.NamedTemporaryFile(suffix='.wav', prefix='ffv-reply-',
                                            dir=get_astrbot_temp_path(), delete=False) as handle:
                handle.write(audio)
                path = handle.name
            event.track_temporary_local_file(path)
            # Keep text for the user and existing memory hooks; QQ sends the Record
            # through its existing adapter, so only the incoming chat receives it.
            result.chain.append(Record(file=path, url=path, text=text))
        except (aiohttp.ClientError, asyncio.TimeoutError, OSError, ValueError):
            logger.warning('[Firefly Voice] 本地语音合成不可用，保留 QQ 文字回复')

    @filter.after_message_sent()
    async def update_shared_history(self, event):
        if event.unified_msg_origin == self.umo:
            self.schedule_history()
