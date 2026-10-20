"""Website dialogue ingress and a bounded display cache; AstrBot owns history."""
import json
import time
import uuid
from collections import OrderedDict

from fastapi import HTTPException, Request
from fastapi.responses import JSONResponse


async def read_json(request, limit=1024 * 1024):
    raw = bytearray()
    async for chunk in request.stream():
        raw.extend(chunk)
        if len(raw) > limit:
            raise HTTPException(413, '请求过大')
    try:
        data = json.loads(raw)
        if not isinstance(data, dict):
            raise ValueError()
        return data
    except (ValueError, TypeError):
        raise HTTPException(400, '需要 JSON 对象') from None


def register_dialogue_routes(app, state, events):
    cache = {'ready': False, 'revision': '', 'messages': [], 'updated_at': 0}
    requests = OrderedDict()
    state['conversation'] = cache

    def expire():
        now = time.time()
        for item in requests.values():
            if item['phase'] in ('queued', 'thinking') and now - item['time'] > 240:
                item.update(phase='error', error='本轮等待超时，请检查 AstrBot；不会自动重发')

    def get_snapshot():
        expire()
        bridge = state['dialogue']
        return dict(cache, connected=time.time() - bridge['last_seen'] < 30,
                    requests=list(requests.values())[-8:])

    @app.get('/v1/dialogue')
    async def snapshot():
        return get_snapshot()

    @app.post('/v1/dialogue/snapshot')
    async def sync_history(request: Request):
        data = await read_json(request)
        messages = data.get('messages')
        revision = data.get('revision')
        if not isinstance(messages, list) or len(messages) > 40 or not isinstance(revision, str) or len(revision) > 128:
            raise HTTPException(400, '对话快照无效')
        clean = []
        for row in messages:
            if not isinstance(row, dict) or row.get('role') not in ('user', 'assistant') or not isinstance(row.get('content'), str) or len(row['content']) > 6000:
                raise HTTPException(400, '对话消息无效')
            clean.append({'role': row['role'], 'content': row['content']})
        cache.update(ready=True, revision=revision, messages=clean, updated_at=time.time())
        return {'ok': True}

    @app.post('/v1/dialogue/messages')
    async def submit(request: Request):
        data = await read_json(request, 16000)
        text, key = data.get('text'), data.get('request_id')
        if not isinstance(text, str) or not text.strip() or len(text) > 2000:
            raise HTTPException(400, '消息长度需为 1–2000 字')
        try:
            if str(uuid.UUID(key)) != key:
                raise ValueError()
        except (ValueError, TypeError, AttributeError):
            raise HTTPException(400, 'request_id 需要 UUID') from None
        expire()
        if key in requests:
            if requests[key]['text'] != text.strip():
                raise HTTPException(409, '请求编号已经用于其他消息')
            return requests[key]
        bridge = state['dialogue']
        if time.time() - bridge['last_seen'] >= 30 or bridge.get('protocol') != 2:
            raise HTTPException(503, 'AstrBot 网页桥接未就绪，请启动或重载语音插件')
        if sum(row['phase'] in ('queued', 'thinking') for row in requests.values()) >= 2:
            raise HTTPException(429, '请等当前对话完成')
        item = {'session': key, 'text': text.strip(), 'source': 'web', 'phase': 'queued', 'time': time.time(), 'reply': '', 'error': ''}
        requests[key] = item
        while len(requests) > 64:
            requests.popitem(last=False)
        events.publish('dialogue.request', dict(item))
        return JSONResponse(item, status_code=202)

    @app.post('/v1/dialogue/result')
    async def result(request: Request):
        data = await read_json(request, 64000)
        item = requests.get(data.get('session'))
        if item is None:
            raise HTTPException(404, '请求已失效')
        if data.get('phase') not in ('thinking', 'complete', 'error'):
            raise HTTPException(400, '状态无效')
        for key in ('reply', 'error'):
            if not isinstance(data.get(key, ''), str) or len(data.get(key, '')) > 6000:
                raise HTTPException(400, '回复无效')
        # Expired requests cannot silently return as a new answer after cancellation.
        if item['phase'] == 'error':
            return {'ok': True, 'ignored': True}
        item.update(phase=data['phase'], reply=data.get('reply', ''), error=data.get('error', ''))
        return {'ok': True}

    return get_snapshot
