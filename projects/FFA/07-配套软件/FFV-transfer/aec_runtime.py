"""Bounded speaker reference timeline and independent, streaming dual-mic AEC.

The network owner supplies sent PCM and consumed-frame telemetry. Only the
audio worker runs AEC/correlation. No HTTP subscriber is in the recognition path.
"""
import threading
import time
import numpy as np
from scipy.signal import firwin, lfilter, butter, sosfilt, correlate, fftconvolve
from aec_processor import EchoCanceller


class RenderReference:
    def __init__(self, seconds=20):
        self.lock = threading.Lock()
        self.ring = np.zeros(16000*seconds, np.float32)
        self.epoch = 0
        self.end = 0
        self.active = self.running = False
        self.played = 0
        self.updated = time.monotonic()
        self.rate = 48000

    def begin(self, rate):
        if rate not in (32000,48000):raise ValueError('invalid render rate')
        with self.lock:
            self.rate = rate; self.factor = rate//16000
            self.taps = firwin(63, 1/self.factor).astype(np.float32)
            self.history = np.zeros(62); self.phase = 0
            self.epoch += 1; self.end = self.played = 0
            self.active = True; self.running = False; self.updated = time.monotonic()

    def append(self, body):
        # Small stateful FIR at the send seam; no FFT/model/network calls here.
        with self.lock:
            if not self.active:return
            values = np.frombuffer(body,dtype='<i2').astype(np.float32)/32768
            filtered,self.history=lfilter(self.taps,[1.],values,zi=self.history)
            output=filtered[self.phase::self.factor].astype(np.float32)
            self.phase=(self.phase-len(values))%self.factor
            indices=(self.end+np.arange(len(output)))%len(self.ring)
            self.ring[indices]=output;self.end+=len(output)

    def progress(self, played, flags=0):
        with self.lock:
            position=int(played*16000/self.rate)
            was_running=self.running
            if flags & (2|32):self.active=False;self.running=False
            elif flags & (4|16):self.running=False
            elif flags & 8 or position>self.played:self.running=True
            if self.running != was_running:self.epoch+=1
            self.played=position;self.updated=time.monotonic()

    def invalidate(self):
        with self.lock:self.epoch+=1

    def finish(self):
        with self.lock:
            self.active=self.running=False;self.epoch+=1;self.updated=time.monotonic()

    def context(self, timestamp=None):
        now=time.monotonic() if timestamp is None else timestamp
        with self.lock:
            advance=max(0,min(.5,now-self.updated))*16000 if self.running else 0
            return {'epoch':self.epoch,'active':self.active,'running':self.running,
                    'position':min(self.end,self.played+advance),
                    'stale':self.running and now-self.updated>1.5}

    def read(self, start, count, epoch):
        with self.lock:
            if epoch!=self.epoch:return None
            first=max(0,self.end-len(self.ring));last=self.end
            begin=max(start,first);end=min(start+count,last)
            output=np.zeros(count,np.float32)
            if end>begin:
                output[begin-start:end-start]=self.ring[np.arange(begin,end)%len(self.ring)]
            # Future samples and samples preceding stream start are defined silence.
            if start<first and first>0:return None
            return output


class ReferenceAlignment:
    """Confirm correlation peaks before moving a running AEC reference clock.

    Nearby room-reflection peaks are not clock jumps. Keep up to 10 ms of
    tracking jitter inside the native adaptive filter's 50 ms reference lead.
    """
    def __init__(self):
        self.clear()

    def clear(self):
        self.offset = None
        self.candidate = None
        self.confirmations = 0

    def observe(self, estimate):
        if self.offset is not None and abs(estimate-self.offset) <= 160:
            self.candidate = None; self.confirmations = 0
            return False
        if self.candidate is None or abs(estimate-self.candidate) > 32:
            self.candidate = estimate; self.confirmations = 1
        else:
            self.candidate = round((self.candidate*self.confirmations+estimate)/(self.confirmations+1))
            self.confirmations += 1
        required = 2 if self.offset is None else 3
        if self.confirmations < required:
            return False
        self.offset = self.candidate
        self.candidate = None; self.confirmations = 0
        return True

    def miss(self):
        self.candidate = None; self.confirmations = 0


class RuntimeAEC:
    def __init__(self):
        self.reference=RenderReference()
        self.lock=threading.Lock();self.enabled=True
        self.data={'enabled':True,'status':'loading','error':'','aligned':False,
                   'confidence_left':0.,'confidence_right':0.,'resets':0,
                   'processed_frames':0,'input_gaps':0,'mode':'dual-independent',
                   'last_update':0.}
        self.engine=None;self.epoch=None;self.last_sequence=None;self.total=0
        self.tracker=ReferenceAlignment();self.window=np.empty((0,2),np.float32);self.last_search=0
        self.sos=butter(3,[150,6000],fs=16000,btype='bandpass',output='sos')

    def set_enabled(self, enabled):
        with self.lock:self.enabled=bool(enabled);self.data['enabled']=self.enabled
        self.reference.invalidate()

    def snapshot(self):
        with self.lock:result=dict(self.data)
        if result['last_update'] and time.time()-result['last_update']>1:
            result.update(status='waiting',aligned=False)
        return result

    def reset(self):
        self.epoch=None;self.last_sequence=None;self.tracker.clear()
        self.window=np.empty((0,2),np.float32);self.engine=None

    @property
    def alignment(self):
        return self.tracker.offset

    @staticmethod
    def pcm(samples):
        return np.clip(np.rint(samples*32768),-32768,32767).astype(np.int16)

    def process(self, samples, sequence, context, overflow=False):
        with self.lock:enabled=self.enabled
        gap=self.last_sequence is not None and sequence!=((self.last_sequence+1)&0xffffffff)
        self.last_sequence=sequence;self.total+=len(samples)
        changed=self.epoch!=context['epoch'] or gap or overflow
        if changed:
            self.tracker.clear();self.window=np.empty((0,2),np.float32)
            self.epoch=context['epoch'];self.engine=None;self.last_search=self.total
            with self.lock:
                self.data['resets']+=1;self.data['input_gaps']+=int(gap or overflow)
        if not enabled:
            with self.lock:self.data.update(status='disabled',aligned=False,last_update=time.time(),error='')
            return samples
        try:
            if self.engine is None:self.engine=EchoCanceller(2,50)
            running=context['active'] and context['running'] and not context['stale']
            self.window=np.concatenate((self.window,samples))[-12800:]
            if running and len(self.window)==12800 and self.total-self.last_search>=8000:
                self.last_search=self.total
                predicted=(self.total-len(self.window)-self.alignment if self.alignment is not None
                           else context['position']-len(self.window))
                margin=800 if self.alignment is not None else 12000
                low=max(0,int(predicted-margin));count=len(self.window)+2*margin
                candidate=self.reference.read(low,count,self.epoch)
                if candidate is not None:
                    candidate=sosfilt(self.sos,candidate)
                    power=fftconvolve(candidate*candidate,np.ones(len(self.window)),mode='valid')
                    matches=[]
                    for channel in range(2):
                        y=sosfilt(self.sos,self.window[:,channel]);energy=np.sum(y*y)
                        scores=np.abs(correlate(candidate,y,mode='valid',method='fft'))/np.sqrt(np.maximum(power*energy,1e-15))
                        index=int(np.argmax(scores));matches.append((float(scores[index]),index+low))
                    with self.lock:self.data.update(confidence_left=round(matches[0][0],3),confidence_right=round(matches[1][0],3))
                    if min(x[0] for x in matches)>.32 and abs(matches[0][1]-matches[1][1])<=16:
                        estimate=self.total-len(self.window)-matches[0][1]
                        if self.tracker.observe(estimate):
                            self.engine=EchoCanceller(2,50)
                            with self.lock:self.data['resets']+=1
                    else:
                        self.tracker.miss()
                        if self.alignment is not None and abs((self.total-self.alignment)-context['position'])>16000:
                            self.tracker.clear();self.engine=EchoCanceller(2,50)
                            with self.lock:self.data['resets']+=1
                else:self.tracker.miss()
            reverse=np.zeros(160,np.float32)
            mode='standby'
            if running:
                position=(self.total-160-self.alignment if self.alignment is not None
                          else int(context['position'])-160)
                reference=self.reference.read(position+800,160,self.epoch)
                if reference is None:
                    self.tracker.clear();mode='aligning'
                else:
                    reverse=reference;mode='active' if self.alignment is not None else 'aligning'
            elif context['stale']:mode='waiting-reference'
            elif not context['active']:self.tracker.clear()
            output=self.engine.process(self.pcm(samples),self.pcm(reverse)).astype(np.float32)/32768
            with self.lock:
                self.data.update(status=mode,aligned=mode=='active',error='',last_update=time.time())
                scale=getattr(self.engine,'capture_scale',None)
                if scale is not None:
                    self.data['capture_gain_left']=round(float(scale.gains[0]),3)
                    self.data['capture_gain_right']=round(float(scale.gains[1]),3)
                self.data['processed_frames']+=1
            return output
        except Exception as exc:
            with self.lock:self.data.update(status='error',aligned=False,error=str(exc)[:240],last_update=time.time())
            self.engine=None
            return samples  # Explicitly reported raw fallback; never mute the user's voice.
