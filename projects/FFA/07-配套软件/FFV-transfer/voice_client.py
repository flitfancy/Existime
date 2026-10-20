"""Small stdlib client for local AI integrations; no model/provider configuration."""
import json
import time
import urllib.error
import urllib.request


BASE='http://127.0.0.1:7865'


def events(after=None):
    headers={'Accept':'text/event-stream'}
    if after:headers['Last-Event-ID']=after
    request=urllib.request.Request(BASE+'/v1/voice/events',headers=headers)
    with urllib.request.urlopen(request,timeout=25) as response:
        lines=[]
        for raw in response:
            line=raw.decode('utf-8').rstrip('\r\n')
            if line.startswith('data:'):lines.append(line[5:].lstrip())
            elif not line and lines:
                yield json.loads('\n'.join(lines))
                lines=[]


def start_instruction():
    request=urllib.request.Request(BASE+'/voice/start',data=b'',method='POST')
    with urllib.request.urlopen(request,timeout=5) as response:return json.load(response)


def play_pcm(body,rate=48000):
    if rate not in (32000,48000):raise ValueError('Expected 32000 or 48000 Hz mono PCM16')
    if not body or len(body)%2:raise ValueError('Expected non-empty PCM16 bytes')
    request=urllib.request.Request(BASE+'/v1/audio/output?rate='+str(rate),data=body,method='POST',
                                   headers={'Content-Type':'application/octet-stream'})
    with urllib.request.urlopen(request,timeout=660) as response:return json.load(response)


if __name__=='__main__':
    cursor=None
    while True:
        try:
            for event in events(cursor):
                # A real consumer should persist this ID after successful handling.
                print(json.dumps(event,ensure_ascii=False),flush=True)
                cursor=event['id']
        except (urllib.error.URLError,OSError):time.sleep(1)
