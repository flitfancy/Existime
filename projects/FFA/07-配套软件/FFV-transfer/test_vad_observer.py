import json,time
from pathlib import Path
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from vad_observer import StereoResampler, SileroCpu, SpeechActivity, VadObserver

def main():
    rng=np.random.default_rng(11);samples=rng.normal(0,.02,(4800,2)).astype(np.float32)
    one=StereoResampler().process(samples);stream=StereoResampler()
    split=np.concatenate([stream.process(samples[i:i+240]) for i in range(0,len(samples),240)])
    assert np.allclose(one,split,atol=1e-6),'resampler state lost across packets'
    gate=SpeechActivity()
    assert gate.update(.9)=='' and gate.update(.9)=='' and gate.update(.9)=='speech-start'
    for _ in range(15):assert gate.update(.1)==''
    assert gate.update(.1)=='speech-end'
    observer=VadObserver()
    for i in range(100):observer.offer(i,bytes(960),False,'both')
    assert observer.queue.qsize()<=32 and observer.snapshot()['queue_drops']>0
    observer.reset();assert observer.queue.empty() and not observer.snapshot()['speech']

    import ctypes
    class MemoryCounters(ctypes.Structure):
        _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[
            (name,ctypes.c_size_t) for name in ('peak','working','qpp','qpu','qnp','qnu','page','peakpage')]
    def rss():
        counters=MemoryCounters();counters.cb=ctypes.sizeof(counters)
        kernel=ctypes.WinDLL('kernel32');kernel.GetCurrentProcess.restype=ctypes.c_void_p
        ctypes.WinDLL('psapi').GetProcessMemoryInfo(ctypes.c_void_p(kernel.GetCurrentProcess()),
            ctypes.byref(counters),counters.cb)
        return counters.working
    rss_before=rss()
    model=SileroCpu()
    for _ in range(10):model.probabilities(np.zeros((512,2),np.float32))
    rss_after=rss()
    model.reset()
    audio,rate=sf.read(__import__('os').environ['FFV_ASR_SELF_TEST_WAV'],dtype='float32')
    if audio.ndim>1:audio=audio.mean(axis=1)
    if rate!=24000:audio=resample_poly(audio,24000,rate)
    audio=np.concatenate((np.zeros(24000),audio,np.zeros(24000))).astype(np.float32)
    stereo=np.stack((audio,np.zeros_like(audio)),axis=1)
    converter=StereoResampler();buf=np.empty((0,2),np.float32);prob=[];timings=[]
    started=time.perf_counter();cpu=time.process_time()
    for offset in range(0,len(stereo),240):
        buf=np.concatenate((buf,converter.process(stereo[offset:offset+240])))
        while len(buf)>=512:
            t=time.perf_counter();prob.append(model.probabilities(buf[:512]));buf=buf[512:]
            timings.append((time.perf_counter()-t)*1000)
    cpu=time.process_time()-cpu;elapsed=time.perf_counter()-started
    probabilities=np.array(prob)
    assert probabilities[:25,0].max()<.2,'silence detected as speech'
    assert probabilities[:,0].max()>.7,'reference voice was not detected'
    assert probabilities[:,1].max()<.2,'silent channel contaminated by voice channel'
    result={'model':'Silero VAD 6.2.1 / ONNX CPU single thread / two channels',
        'audio_seconds':len(audio)/24000,'processed_seconds':elapsed,
        'mean_inference_ms_per_32ms':float(np.mean(timings)),
        'p95_inference_ms_per_32ms':float(np.percentile(timings,95)),
        'cpu_one_core_percent_equivalent':100*cpu/(len(audio)/24000),
        'incremental_rss_mb':(rss_after-rss_before)/1024**2,
        'voice_max_probability':float(probabilities[:,0].max()),
        'silent_channel_max_probability':float(probabilities[:,1].max())}
    path=Path(__file__).parent/'diagnostics/vad-benchmark.json'
    path.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
