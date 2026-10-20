"""Check actual AEC timeline, channel paths and recording routing without hardware."""
import json,time
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from aec_runtime import RuntimeAEC,RenderReference
from vad_observer import StereoResampler

def main():
    # Fragmented reference appends must equal a single append, with bounded storage.
    rng=np.random.default_rng(40);body=rng.integers(-10000,10000,48000,dtype=np.int16).tobytes()
    a=RenderReference();b=RenderReference();a.begin(48000);b.begin(48000)
    a.append(body)
    for i in range(0,len(body),8192):b.append(body[i:i+8192])
    assert a.end==b.end==16000
    assert np.allclose(a.read(0,a.end,a.epoch),b.read(0,b.end,b.epoch),atol=1e-7)
    before=b.epoch;b.progress(4800);assert b.context()['running'] and b.epoch>before
    before=b.epoch;b.progress(4800,4);assert not b.context()['running'] and b.epoch>before
    b.progress(4800,8);assert b.context()['running']
    b.finish();assert not b.context()['active']
    assert b.read(0,160,b.epoch-1) is None,'old stream reference must not leak'

    # Replay measured room echo through the SAME runtime as the web service.
    root=Path(__file__).parent/'diagnostics'
    mic,rate=sf.read(root/'aec-microphones.wav',dtype='float32',always_2d=True)
    render,ref_rate=sf.read(root/'aec-reference.wav',dtype='float32')
    assert rate==24000
    render48=resample_poly(render,48000,ref_rate)
    runtime=RuntimeAEC();runtime.reference.begin(48000)
    runtime.reference.append(np.clip(np.rint(render48*32768),-32768,32767).astype('<i2').tobytes())
    offset=47347/24000  # measured sample fixture start, telemetry is deliberately coarse
    converter=StereoResampler();raw=[];clean=[];states=[]
    for i in range(len(mic)//240):
        timestamp=i*.01;played=max(0,min(len(render48),int((timestamp-offset)*48000)))
        if i%25==0:runtime.reference.progress(played)
        context=runtime.reference.context()
        context['position']=played/3  # deterministic coarse board clock for fixture
        x=converter.process(mic[i*240:(i+1)*240]);raw.append(x)
        clean.append(runtime.process(x,i,context));states.append(runtime.snapshot())
    raw=np.concatenate(raw);clean=np.concatenate(clean)
    region=slice(int((offset+1.8)*16000),int((offset+4.6)*16000))
    reduction=10*np.log10((np.mean(raw[region]**2,axis=0)+1e-12)/(np.mean(clean[region]**2,axis=0)+1e-12))
    assert any(s['status']=='active' for s in states),'runtime never aligned'
    assert min(reduction)>6,('runtime echo checkpoint failed',reduction)
    runtime.set_enabled(False)
    x=rng.normal(0,.02,(160,2)).astype(np.float32)
    assert np.array_equal(x,runtime.process(x,5000,runtime.reference.context()))
    runtime.set_enabled(True)
    runtime.process(x,5002,runtime.reference.context())
    assert runtime.snapshot()['input_gaps']>=1,'capture discontinuity must reset alignment'
    result={'runtime_echo_reduction_db':reduction.tolist(),'timeline_fragmentation':True,
            'pause_resume_stop_epoch':True,'disabled_raw_bypass':True,'sequence_gap_reset':True}
    (root/'aec-runtime-test.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
