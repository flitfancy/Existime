"""Read the live page state for one full cycle; does not change UI settings."""
import argparse
import http.client
import json
from pathlib import Path
import time

parser=argparse.ArgumentParser()
parser.add_argument('host')
parser.add_argument('--version',default='1.3.13')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()

def get(path):
    c=http.client.HTTPConnection(args.host,80,timeout=8)
    try:
        c.request('GET',path)
        r=c.getresponse()
        data=json.loads(r.read())
        if r.status!=200: raise RuntimeError(f'{path}: {r.status}')
        data.pop('ota_nonce',None)
        return data
    finally:
        c.close()

before=get('/device')
assert before['firmware_version']==args.version
start=time.monotonic()
transitions=[]
seen=set()
expected={'environment','pressure','light','gas','spectrum'}
previous=None
while time.monotonic()-start < 52:
    d=get('/display')
    assert d['auto_cycle'] and d['page_duration_ms']==8000 and d['x3_enable_bit'] and d['init_sent']
    if d['heart_in_cycle']: expected.add('heart')
    elif d['page']=='heart': raise RuntimeError('Disconnected watch still in automatic cycle')
    if d['page']!=previous:
        row={'elapsed_s':round(time.monotonic()-start,2),'page':d['page'],
             'page_count':d['page_count'],'values':d['page_values'],'x3_on':d['x3_enable_bit']}
        transitions.append(row);print(json.dumps(row,ensure_ascii=False),flush=True)
        seen.add(d['page']);previous=d['page']
    if expected <= seen and len(transitions)>=len(expected)+1: break
    time.sleep(1)
after=get('/device')
assert after['firmware_version']==args.version and after['uptime_ms']>before['uptime_ms']
assert expected <= seen,(expected,seen)
# The first partial page isn't timed; the subsequent full pages should be ~8s.
intervals=[round(transitions[i]['elapsed_s']-transitions[i-1]['elapsed_s'],2)
           for i in range(2,len(transitions))]
assert all(7<=duration<=11 for duration in intervals),intervals
result={'verdict':'PASS_PAGE_STATE_AND_UPTIME','is_pixel_readback':False,
        'before_device':before,'after_device':after,'pages_seen':sorted(seen),
        'full_page_intervals_s':intervals,'transitions':transitions}
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'verdict':result['verdict'],'pages_seen':result['pages_seen'],'intervals':intervals}),flush=True)
