"""Persistent local Wi-Fi audio service for dual microphones, STT, and playback."""
import os
os.environ.setdefault('HF_HUB_OFFLINE','1')
os.environ.setdefault('MODELSCOPE_OFFLINE','1')

import asyncio,ipaddress,json,queue,socket,struct,threading,time,traceback,wave,zlib
from contextlib import asynccontextmanager
from pathlib import Path

import numpy as np
import serial
from serial.tools import list_ports
from fastapi import FastAPI,HTTPException,Request
from fastapi.responses import HTMLResponse,FileResponse,JSONResponse,StreamingResponse
import uvicorn

from media_decode import decode_to_pcm
from lyrics import load_lyrics
from ffv_v2_client import FireflyVoiceConnection
from board_discovery import discover_board_ipv4, is_lan_ipv4
from protocol_v2 import FrameType
from vad_observer import VadObserver
from aec_runtime import RuntimeAEC
from playback_spectrum import PlaybackSpectrum
from recognition_segments import UtteranceBuffer
from wakeword import KEYWORD, WakeWord
from voice_turn import VoiceTurn, wake_chime
from voice_events import VoiceEvents
from speech_services import TranscriptionJob, register_speech_routes
from character_voice import character_voice
from dialogue_service import register_dialogue_routes
from ota_service import OtaManager, register_ota_routes
from output_scheduler import OutputScheduler, OutputCancelled, SpeechStream
from output_session import OutputSession
from playback_routes import PlaybackAPI
from microphone_history import MicrophoneHistory, register_history_routes

ROOT=Path(__file__).parent
microphone_history=MicrophoneHistory(ROOT/'data'/'microphone-history.sqlite3')
RECORDINGS=ROOT/'recordings';RECORDINGS.mkdir(exist_ok=True)
PAIRING_KEY_FILE=Path(os.environ.get('FFV_PAIRING_KEY_FILE', str(ROOT/'media-work'/'wifi-pairing.key')))
MODEL=os.environ.get('FFV_ASR_MODEL', str(ROOT/'models'/'SenseVoiceSmall'))
MIC_RATE=24000
PLAY_RATE=48000
WIFI_MIC_PORT=7867


BOARD_SERIAL=os.environ.get('FFV_BOARD_SERIAL', '')
BOARD_HOST=os.environ.get('FFV_BOARD_HOST', 'firefly-voice.local')
MIC_MODES={
    'left':{'command':b'1','channels':1,'label':'左麦 U6 / GPIO5'},
    'right':{'command':b'2','channels':1,'label':'右麦 U7 / GPIO1'},
    'both':{'command':b'3','channels':2,'label':'双麦 U6 + U7'},
}
GAINS=(1,2,4,8)

state={
    'model':'loading','recording':False,'mic_mode':'both','mic':MIC_MODES['both']['label'],'channels':2,'gain':4,
    'rms_db':-100,'rms_db_left':-100,'rms_db_right':-100,'peak':0,'frames':0,
    'crc_errors':0,'gaps':0,'overruns':0,'queue_drops':0,'texts':[],'error':'','wav':'','seconds':0,
    'playing':False,'play_paused':False,'play_name':'','play_progress':0,'play_position':0,
    'play_duration':0,'play_status':'等待拖入音频','play_error':'','lyrics':[],'lyrics_source':'',
    'play_volume':100,'play_rate':PLAY_RATE,'buffered_frames':0,'ring_capacity_frames':0,
    'retry_count':0,'rebuffer_events':0,'metadata':None,'result':None,
    'active_transport':'offline','connection_state':'starting','reconnect_count':0,
    'device_error':'','input_streaming':False,'input_subscribers':0,'input_packets':0,
    'input_gaps':0,'input_overruns':0,'input_drops':0,'output_mode':'idle',
    'heartbeat_acks':0,'last_heartbeat':0,
    'wifi_configured':False,
    'wifi_connected':False,'wifi_ip':'','wifi_rssi':None,'wifi_audio_port':7866,
    'upload_active':False,'upload_received':0,'upload_total':0,
}
jobs=queue.Queue(maxsize=8)


session_commands=queue.SimpleQueue()
device_commands=queue.SimpleQueue()
capture_stop=threading.Event()
model=None
workers_started=threading.Event()
last_live_sequence=None
active_voice_turn=None
voice_events=VoiceEvents()

def publish_voice_turn(turn):
    kind='voice.turn.completed' if turn['phase']=='complete' else 'voice.turn.ended'
    voice_events.publish(kind,turn)
aec_runtime=RuntimeAEC()
playback_spectrum=PlaybackSpectrum(aec_runtime.reference)
wakeword=WakeWord(lambda event:session_commands.put(('wake',event)), ROOT/'media-work'/'voice-settings.json')
vad_observer=VadObserver(processor=aec_runtime,wakeword=wakeword)

def discover_wifi_ip():
    address=discover_board_ipv4(BOARD_HOST,state['wifi_audio_port'])
    state.update(wifi_configured=bool(address),wifi_connected=False,wifi_ip=address)
    return address

@asynccontextmanager
async def lifespan(_app):
    if not workers_started.is_set():
        workers_started.set()
        microphone_history.start()
        wakeword.start()
        vad_observer.start()
        threading.Thread(target=asr_worker,daemon=True).start()
        threading.Thread(target=device_session.run,daemon=True,name='ffv-device-session').start()
    try:
        yield
    finally:
        microphone_history.close()
        await asyncio.to_thread(character_voice.close)

app=FastAPI(lifespan=lifespan)
register_history_routes(app,microphone_history)
ota_manager=OtaManager()
output_scheduler=OutputScheduler(state,blocked=lambda:ota_manager.snapshot()['busy'])
register_ota_routes(app,ota_manager,state,activity_lock=output_scheduler.lock)
register_speech_routes(app,jobs,state,voice_events)
dialogue_snapshot=register_dialogue_routes(app,state,voice_events)
WORK_ROOT=ROOT/'media-work';WORK_ROOT.mkdir(exist_ok=True)
output_api=PlaybackAPI(app,output_scheduler,WORK_ROOT,decode_to_pcm,load_lyrics,rate=PLAY_RATE)

def local_host(header):
    value=(header or '').strip().lower().rstrip('.')
    if value.startswith('['):
        end=value.find(']');value=value[1:end] if end>=0 else value
    else:
        base,sep,port=value.rpartition(':')
        if sep and port.isdigit():value=base
    return value in {'127.0.0.1','localhost','::1'}

@app.middleware('http')
async def protect_local_service(request:Request,call_next):
    peer=request.client.host if request.client else ''
    try:peer_is_loopback=ipaddress.ip_address(peer).is_loopback
    except ValueError:peer_is_loopback=False
    if not peer_is_loopback or not local_host(request.headers.get('host')):
        return JSONResponse({'detail':'local access only'},status_code=403)
    sec_fetch=(request.headers.get('sec-fetch-site') or '').lower()
    origin=(request.headers.get('origin') or '').strip()
    if sec_fetch=='cross-site' or (origin and origin not in {
        'http://127.0.0.1:7865','http://localhost:7865','http://[::1]:7865',
    }):
        return JSONResponse({'detail':'cross-site request rejected'},status_code=403)
    if request.method=='POST' and request.url.path!='/device/firmware' and ota_manager.snapshot()['busy']:
        return JSONResponse({'detail':'固件正在升级，请等待完成后再控制音频'},status_code=409)
    response=await call_next(request)
    response.headers['Cache-Control']='no-store'
    response.headers['X-Content-Type-Options']='nosniff'
    response.headers['X-Frame-Options']='DENY'
    response.headers['Referrer-Policy']='no-referrer'
    return response

def find_board_port():
    ports=[p for p in list_ports.comports() if (p.serial_number or '').replace(':','').lower()==BOARD_SERIAL]
    if len(ports)!=1:raise RuntimeError('Expected FFA module 80:45:6b:28:17:5c was not found uniquely')
    return ports[0].device

def find_board_port_or_none():
    try:return find_board_port()
    except Exception:return None

class SocketConnection(FireflyVoiceConnection):
    def __init__(self,host,port,mic_port=WIFI_MIC_PORT):
        super().__init__(host,port,pairing_key_file=PAIRING_KEY_FILE,mic_port=mic_port)


def open_board():
    cached=state['wifi_ip']
    if is_lan_ipv4(cached):
        try:
            connection=SocketConnection(cached,state['wifi_audio_port'])
        except (OSError,RuntimeError):
            state.update(wifi_connected=False,wifi_ip='')
        else:
            state.update(wifi_connected=True,active_transport='wifi',connection_state='connected',device_error='')
            return connection
    address=discover_wifi_ip()
    if not address:raise OSError('未发现 firefly-voice.local')
    connection=SocketConnection(address,state['wifi_audio_port'])
    state.update(wifi_connected=True,wifi_ip=address,active_transport='wifi',connection_state='connected',device_error='')
    return connection

def decode_wifi_status(frame):
    if frame.type!=FrameType.DEVICE_STATUS or len(frame.payload)!=16:
        raise ValueError('无效的板端状态帧')
    uptime,rssi,address,connected,authenticated,_=struct.unpack('<IiIBBH',frame.payload)
    ip='.'.join(str((address>>shift)&255) for shift in (24,16,8,0)) if connected else ''
    state.update(wifi_configured=True,wifi_connected=bool(connected),wifi_ip=ip,
                 wifi_rssi=rssi if connected else None,board_uptime_ms=uptime)


def recognize(audio,rate=MIC_RATE):
    from funasr.utils.postprocess_utils import rich_transcription_postprocess
    audio=audio.astype(np.float32)
    if rate!=16000:
        from scipy.signal import resample_poly
        audio=resample_poly(audio,16000,rate)
    audio-=audio.mean();peak=float(np.max(np.abs(audio)));audio*=min(20.,0.8/max(peak,1e-6))
    started=time.monotonic();result=model.generate(input=audio,cache={},language='auto',use_itn=True,disable_pbar=True)
    return rich_transcription_postprocess(result[0]['text']),time.monotonic()-started

def asr_worker():
    global model
    try:
        import torch
        from funasr import AutoModel
        device=os.environ.get('FFV_ASR_DEVICE','cpu').strip()
        if device not in ('cpu','cuda:0'):
            raise ValueError('FFV_ASR_DEVICE must be cpu or cuda:0')
        model=AutoModel(model=MODEL,trust_remote_code=False,device=device,ncpu=4,disable_update=True,disable_pbar=True)
        state['asr_device']=device
        test_wav=os.environ.get('FFV_ASR_SELF_TEST_WAV', '').strip()
        if test_wav:
            import soundfile as sf
            ref,rate=sf.read(test_wav,dtype='float32')
            if ref.ndim>1:ref=ref.mean(axis=1)
            text,elapsed=recognize(ref,rate)
            (ROOT/'stt-self-test.json').write_text(json.dumps({'text':text,'seconds':elapsed},ensure_ascii=False,indent=2),encoding='utf-8')
            state['self_test']=text
        else:
            state['self_test']='not requested'
        state['model']='ready';print('MODEL_READY',flush=True)
    except Exception:
        state['model']='error';state['error']=traceback.format_exc();print(state['error'],flush=True);return
    while True:
        job=jobs.get()
        if isinstance(job,TranscriptionJob):
            try:
                if job.future.set_running_or_notify_cancel():
                    try:job.future.set_result(recognize(job.audio,job.rate)[0])
                    except Exception as exc:job.future.set_exception(exc)
            finally:jobs.task_done()
            continue
        session_id,index,audio,source,overlaps_playback,rate,turn=job
        try:
            text,elapsed=recognize(audio,rate)
            row={'session':session_id,'chunk':index,'source':source,'playback_overlap':overlaps_playback,'text':text,'inference_seconds':round(elapsed,3)}
            if state.get('capture_session_id')==session_id:
                state['texts'].append(row);state['texts']=state['texts'][-100:]
            with (RECORDINGS/(session_id+'.jsonl')).open('a',encoding='utf-8') as f:f.write(json.dumps(row,ensure_ascii=False)+'\n')
            print(json.dumps(row,ensure_ascii=False),flush=True)
            voice_events.publish('transcript.final',dict(row,mode='instruction' if turn else 'continuous'))
            if turn:turn.result(text)
        except Exception:
            state['error']=traceback.format_exc()
            if turn:turn.result(error='本地语音识别失败')
        finally:jobs.task_done()

def channel_level(samples):
    samples=samples.astype(np.float32);ac=samples-samples.mean();rms=float(np.sqrt(np.mean(ac*ac)))/32768
    return round(20*np.log10(max(rms,1e-5)),1),int(np.max(np.abs(samples)))

def enqueue(session_id,index,raw,channels,overlaps_playback,rate=MIC_RATE,scores=None,turn=None):
    matrix=np.frombuffer(raw,dtype='<i2').astype(np.float32).reshape(-1,channels)/32768.
    if channels==1:audio=matrix[:,0];source=state['mic_mode']
    else:
        if scores is None:
            size=rate//50;usable=(len(matrix)//size)*size;framed=matrix[:usable].reshape(-1,size,channels)
            centered=framed-framed.mean(axis=1,keepdims=True);scores=np.sqrt(np.mean(centered*centered,axis=(0,1)))
        selected=int(np.argmax(scores));audio=matrix[:,selected];source='left' if selected==0 else 'right'
    audio=audio-audio.mean()
    if np.sqrt(np.mean(audio*audio))<0.0005:return False
    if turn:turn.queued()
    try:jobs.put_nowait((session_id,index,audio,source,overlaps_playback,rate,turn))
    except queue.Full:
        state['queue_drops']+=1
        if turn:turn.result(error='语音识别队列已满')
        return False
    return True

class MicBroadcaster:
    def __init__(self):
        self.lock=threading.Lock();self.subscribers=set()

    def subscribe(self,loop):
        target=asyncio.Queue(maxsize=12);subscriber=(loop,target)
        with self.lock:
            self.subscribers.add(subscriber);state['input_subscribers']=len(self.subscribers)
        return subscriber

    def unsubscribe(self,subscriber):
        with self.lock:
            self.subscribers.discard(subscriber);state['input_subscribers']=len(self.subscribers)

    @staticmethod
    def offer(target,payload):
        if target.full():
            try:target.get_nowait();state['input_drops']+=1
            except asyncio.QueueEmpty:pass
        try:target.put_nowait(payload)
        except asyncio.QueueFull:state['input_drops']+=1

    def publish(self,payload):
        state['input_packets']+=1
        with self.lock:subscribers=tuple(self.subscribers)
        stale=[]
        for loop,target in subscribers:
            try:loop.call_soon_threadsafe(self.offer,target,payload)
            except RuntimeError:stale.append((loop,target))
        if stale:
            with self.lock:
                for subscriber in stale:self.subscribers.discard(subscriber)
                state['input_subscribers']=len(self.subscribers)

mic_broadcaster=MicBroadcaster()

def request_wake_feedback(turn):
    speech=SpeechStream(PLAY_RATE)
    speech.feed(wake_chime(PLAY_RATE))
    speech.finish_input()
    output_scheduler.cue(speech,turn)

class MicCapture:
    def __init__(self,seconds,wake=False):
        self.session_id=time.strftime('%Y%m%d-%H%M%S')+'-'+str(time.time_ns()%1000000000).zfill(9)
        self.selected=state['mic_mode'];self.config=MIC_MODES[self.selected]
        self.channels=self.config['channels'];self.gain=state['gain']
        self.deadline=time.monotonic()+seconds;self.raw=bytearray()
        self.last=None;self.first=None;self.index=0;self.active=False
        self.clean=bytearray();self.clean_next=None
        self.utterance=UtteranceBuffer(self.channels,self.selected)
        self.turn=VoiceTurn(self.session_id,on_complete=publish_voice_turn) if wake else None

    def start(self):
        global active_voice_turn
        active_voice_turn=self.turn
        capture_stop.clear()
        wakeword.set_recording(True)
        state.update(recording=True,error='',frames=0,crc_errors=0,gaps=0,overruns=0,
                      queue_drops=0,seconds=0,wav='',wav_raw='',texts=[])
        self.active=True
        state['capture_session_id']=self.session_id
        voice_events.publish('capture.started',{'session':self.session_id,'mode':'instruction' if self.turn else 'continuous'})

    def should_stop(self):
        return capture_stop.is_set() or time.monotonic()>=self.deadline or bool(self.turn and self.turn.stop_reason())

    def stop(self,reason=None):
        if not self.active:return
        self.active=False;state['recording']=False
        wakeword.set_recording(False)
        if self.turn:self.turn.close(reason or ('manual' if capture_stop.is_set() else self.turn.stop_reason()) or 'manual')

    def process(self,frame):
        _,seq,flags,crc,payload=frame
        packet_channels=2 if flags&2 else 1
        if packet_channels!=2 or zlib.crc32(payload)!=crc:
            state['crc_errors']+=1;return
        if self.last is not None:
            missing=(seq-self.last-1)&0xffffffff
            if 0<missing<100000:
                state['gaps']+=missing
                self.raw.extend(bytes(min(missing,60000)*240*2*self.channels))
        self.last=seq
        if self.first is None:self.first=seq
        if flags&1:state['overruns']+=1
        matrix=np.frombuffer(payload,dtype='<i2').reshape(-1,2)
        if self.channels==2:output=matrix
        else:output=matrix[:,0 if self.selected=='left' else 1]
        selected_payload=output.astype('<i2',copy=False).tobytes()
        self.raw.extend(selected_payload)
        left,lp=channel_level(matrix[:,0]);right,rp=channel_level(matrix[:,1])
        state.update(rms_db_left=left,rms_db_right=right,rms_db=max(left,right),
                     peak=max(lp,rp),frames=state['frames']+1)
        state['seconds']=round((len(self.raw)//2/self.channels)/MIC_RATE,2)
    def process_clean(self,seq,samples,probabilities,playback):
        # Runs only on the audio worker; raw capture stays on the network owner.
        expected=self.first if self.clean_next is None else self.clean_next
        missing=(seq-expected)&0xffffffff
        if 0<missing<100000:
            self.submit_utterance(self.utterance.finish())
            silence=bytes(min(missing,60000)*160*2*self.channels)
            self.clean.extend(silence)
        self.clean_next=(seq+1)&0xffffffff
        output=samples if self.channels==2 else samples[:,0 if self.selected=='left' else 1]
        payload=np.clip(np.rint(output*32768),-32768,32767).astype('<i2').tobytes()
        self.clean.extend(payload)
        if self.turn and not self.turn.allows_audio():return
        result=self.utterance.push(payload,probabilities,playback)
        if self.turn:self.turn.observe(self.utterance.active)
        self.submit_utterance(result)
        if self.turn and result is not None and self.utterance.last_reason=='silence':
            self.turn.request_finish()

    def submit_utterance(self,result):
        if result is None:return
        pcm,scores,playback=result
        enqueue(self.session_id,self.index,pcm,self.channels,playback,16000,scores,self.turn)
        self.index+=1

    def finish(self):
        self.submit_utterance(self.utterance.finish())
        if self.turn:self.turn.finalize()
        if not self.raw:return
        path=RECORDINGS/(self.session_id+'.wav')
        desired=(len(self.raw)//(2*self.channels)//240)*160*2*self.channels
        if len(self.clean)<desired:self.clean.extend(bytes(desired-len(self.clean)))
        raw_path=RECORDINGS/(self.session_id+'-raw.wav')
        for destination,data,rate in [(path,self.clean,16000),(raw_path,self.raw,MIC_RATE)]:
            with wave.open(str(destination),'wb') as output:
                output.setnchannels(self.channels);output.setsampwidth(2);output.setframerate(rate)
                output.writeframes(data)
        if state.get('capture_session_id')==self.session_id:
            state.update(wav=path.name,wav_raw=raw_path.name,wav_channels=self.channels)
        snapshot={key:value for key,value in state.items() if key!='texts'}
        (RECORDINGS/(self.session_id+'-stats.json')).write_text(
            json.dumps(snapshot,ensure_ascii=False,indent=2),encoding='utf-8')

def launch_capture(seconds,instruction=False):
    with output_scheduler.lock:
        if state['recording'] or state['model']!='ready':return False
        if not state.get('board_connected') or ota_manager.snapshot()['busy']:return False
        state['recording']=True
        wakeword.set_recording(True)
        session_commands.put(('instruction-start' if instruction else 'mic-start',seconds))
        return True

def update_mic_session(mic,toggle=False):
    if mic and (toggle or mic.should_stop()):
        mic.stop();vad_observer.finish_capture(mic);return None
    if toggle and not mic and state['model']=='ready':
        mic=MicCapture(600);mic.start()
    return mic

def apply_session_commands(mic):
    while True:
        try:command,value=session_commands.get_nowait()
        except queue.Empty:return mic
        if ota_manager.snapshot()['busy']:continue
        if command in ('mic-start','instruction-start') and not mic and state['model']=='ready':
            mic=MicCapture(value,wake=command=='instruction-start');mic.start()
            if mic.turn:request_wake_feedback(mic.turn)
        elif command=='wake' and wakeword.consume(value):
            if not mic and not state['recording'] and state['model']=='ready':
                mic=MicCapture(VoiceTurn.maximum_seconds,wake=True);mic.start()
                request_wake_feedback(mic.turn)

def update_live_input(frame,capture=None):
    global last_live_sequence
    _,seq,flags,crc,payload=frame
    if not flags&2 or zlib.crc32(payload)!=crc:return False
    if last_live_sequence is not None:
        gap=(seq-last_live_sequence-1)&0xffffffff
        if gap<1000000:state['input_gaps']+=gap
    last_live_sequence=seq
    if flags&1:state['input_overruns']+=1
    if capture:capture.process(frame)
    vad_observer.offer(seq,payload,state['playing'],state['mic_mode'],overflow=bool(flags&1),capture=capture)
    matrix=np.frombuffer(payload,dtype='<i2').reshape(-1,2)
    left,lp=channel_level(matrix[:,0]);right,rp=channel_level(matrix[:,1])
    state.update(rms_db_left=left,rms_db_right=right,rms_db=max(left,right),peak=max(lp,rp))
    microphone_history.offer(left,right,len(matrix),MIC_RATE,state['gain'])
    mic_broadcaster.publish(payload)
    return True

class DeviceSession:
    def __init__(self):
        self.connection=None;self.active=None;self.mic=None
        self.last_poll=0.;self.last_heartbeat_ack=0.;self.reconnect_delay=4.
        self.last_input_packet=0.

    def configure_input(self):
        port=self.connection.audio_socket.getsockname()[1]
        self.connection.send_frame(FrameType.MIC_CONFIG,struct.pack('<BBH',1,int(state['gain']),port))

    def connect(self):
        global last_live_sequence
        state.update(connection_state='discovering',active_transport='offline')
        self.connection=open_board();self.configure_input()
        self.connection.send_frame(FrameType.PING)
        self.last_poll=self.last_heartbeat_ack=time.monotonic()
        with output_scheduler.lock:
            state.update(board_connected=True,input_streaming=False,connection_state='connected',
                         active_transport='wifi',device_error='',input_packets=0,input_gaps=0,
                         input_overruns=0,input_drops=0)
            output_scheduler.connected()
        last_live_sequence=None;self.reconnect_delay=4.
        self.last_input_packet=0.
        ota_manager.connected()

    def disconnect(self,message=''):
        with output_scheduler.lock:
            state.update(board_connected=False,wifi_connected=False,input_streaming=False,active_transport='offline',
                         connection_state='disconnected',device_error=str(message)[:300])
            output_scheduler.disconnected(str(message) or 'FIREFLY VOICE 连接中断')
        ota_manager.disconnected()
        aec_runtime.reference.finish()
        vad_observer.reset()
        if self.connection:self.connection.close()
        self.connection=None
        if self.active:
            self.active.finish('播放失败',str(message) or 'FIREFLY VOICE 连接中断');self.active=None
        if self.mic:self.mic.stop('disconnect');vad_observer.finish_capture(self.mic);self.mic=None
        # Queued capture commands belong to the lost connection, not its successor.
        while True:
            try:session_commands.get_nowait()
            except queue.Empty:break
        state['recording']=False
        wakeword.set_recording(False)
        wakeword.invalidate(cooldown=True)

    def start_next_output(self):
        if self.active:return
        task=output_scheduler.take()
        if task is None:return
        try:
            self.active=OutputSession(task,output_scheduler,self.connection,aec_runtime.reference)
        except OutputCancelled:
            output_scheduler.finish(task,'已停止')
        except Exception as exc:
            output_scheduler.finish(task,'播放失败',str(exc))
            raise


    def apply_device_commands(self):
        while True:
            try:command,value=device_commands.get_nowait()
            except queue.Empty:return
            if ota_manager.snapshot()['busy']:continue
            if command=='gain':self.configure_input();aec_runtime.reference.invalidate()
            elif command=='reboot':
                self.connection.send_frame(FrameType.REBOOT)
                raise ConnectionError('板子正在重启')

    def handle_frame(self,frame):
        ota_manager.handle(frame)
        if frame.type==FrameType.DEVICE_STATUS:decode_wifi_status(frame)
        elif frame.type==FrameType.PONG:
            self.last_heartbeat_ack=time.monotonic()
            state['heartbeat_acks']+=1;state['last_heartbeat']=time.time()
        elif frame.type==FrameType.BUTTON and len(frame.payload)==2 and frame.payload[0]==2:
            if not ota_manager.snapshot()['busy']:self.mic=update_mic_session(self.mic,toggle=True)
        elif frame.type==FrameType.ERROR:
            if len(frame.payload)!=12:raise ValueError('无效板端错误帧')
            code,context,_,value,detail=struct.unpack('<BBHII',frame.payload)
            # Reset this connection on semantic errors; no stale audio may reach a later stream.
            raise RuntimeError(f'板端错误 code={code}, context={context}, value={value}, detail={detail}')
        if self.active and self.active.handle(frame):
            self.active=None

    def connected_loop(self):
        while self.connection:
            self.mic=apply_session_commands(self.mic)
            if self.mic:self.mic=update_mic_session(self.mic)
            self.apply_device_commands()
            for frame in self.connection.read_frames():self.handle_frame(frame)
            if ota_manager.take_pending():
                if self.active or state['playing'] or state['recording']:
                    ota_manager.fail('音频任务在上传期间启动，请停止后重新升级')
                    continue
                if self.mic:
                    self.mic.stop('ota');vad_observer.finish_capture(self.mic);self.mic=None
                wakeword.invalidate();vad_observer.reset();aec_runtime.reference.finish()
                self.connection.send_frame(FrameType.MIC_CONFIG,struct.pack('<BBH',0,int(state['gain']),WIFI_MIC_PORT))
                state['input_streaming']=False
                ota_manager.transfer(self.connection,self.handle_frame)
                raise ConnectionError('固件已写入，等待板子重启并验证')
            for _ in range(128):
                frame=self.connection.read_audio()
                if frame is None:break
                if frame[0]=='audio' and update_live_input(frame,self.mic):
                    state['input_streaming']=True
                    self.last_input_packet=time.monotonic()
            self.start_next_output()
            if self.active:self.active.drive()
            now=time.monotonic()
            ota_manager.tick(self.connection,now-self.last_input_packet<1,now-self.last_heartbeat_ack<2)
            if now-self.last_poll>=1:
                self.connection.send_frame(FrameType.PING);self.last_poll=now
            if now-self.last_heartbeat_ack>5:raise TimeoutError('板端心跳超过 5 秒未响应')
            time.sleep(.001)

    def run(self):
        while True:
            try:self.connect();self.connected_loop()
            except Exception as exc:
                print('DEVICE_SESSION '+repr(exc),flush=True);self.disconnect(exc)
                state['reconnect_count']+=1
                time.sleep(self.reconnect_delay);self.reconnect_delay=min(12,self.reconnect_delay*1.5)

device_session=DeviceSession()

@app.get('/',response_class=HTMLResponse)
def index():
    return HTMLResponse((ROOT/'index.html').read_text(encoding='utf-8'),headers={'Cache-Control':'no-store'})

@app.get('/status')
def status():
    with output_scheduler.lock:
        snapshot=dict(state);snapshot.update(output_scheduler.snapshot())
    return dict(snapshot,ota=ota_manager.snapshot(),playback_spectrum=playback_spectrum.snapshot(snapshot['playing'],snapshot['play_paused'],snapshot['play_volume']),conversation=dialogue_snapshot(),queued=jobs.qsize(),port=find_board_port_or_none(),vad=vad_observer.snapshot(),aec=aec_runtime.snapshot(),wakeword=wakeword.snapshot(),voice_turn=active_voice_turn.snapshot() if active_voice_turn else None)

@app.post('/wakeword/{enabled}')
def set_wakeword(enabled:int):
    if enabled not in (0,1):raise HTTPException(400,'Wake word must be 0 or 1')
    wakeword.set_enabled(bool(enabled))
    return {'enabled':bool(enabled),'keyword':KEYWORD}

@app.post('/aec/{enabled}')
def set_aec(enabled:int):
    if enabled not in (0,1):raise HTTPException(400,'AEC must be 0 or 1')
    aec_runtime.set_enabled(bool(enabled))
    return {'enabled':bool(enabled)}

@app.get('/wifi/status')
def wifi_status():
    return {'configured':state['wifi_configured'],'connected':state['wifi_connected'],
            'ip':state['wifi_ip'],'rssi':state['wifi_rssi'],'audio_port':state['wifi_audio_port'],
            'connection_state':state['connection_state']}

@app.post('/mic/{mode}')
def set_mic(mode:str):
    if mode not in MIC_MODES:raise HTTPException(400,'Mode must be left, right, or both')
    if state['recording']:raise HTTPException(409,'Stop recording before changing microphones')
    wakeword.invalidate()
    config=MIC_MODES[mode];state.update(mic_mode=mode,mic=config['label'],channels=config['channels'],rms_db=-100,rms_db_left=-100,rms_db_right=-100)
    return {'mode':mode,'label':config['label'],'channels':config['channels']}

@app.post('/gain/{gain}')
def set_gain(gain:int):
    if gain not in GAINS:raise HTTPException(400,'Gain must be 1, 2, 4, or 8')
    if state['recording']:raise HTTPException(409,'Stop recording before changing gain')
    state['gain']=gain;device_commands.put(('gain',gain));return {'gain':gain}



@app.post('/start')
def start(seconds:int=60):
    if state['recording']:raise HTTPException(409,'麦克风识别已经开启')
    if state['model']!='ready':raise HTTPException(409,'Model is not ready')
    if not 1<=seconds<=600:raise HTTPException(400,'Duration must be 1..600 seconds')
    if not launch_capture(seconds):raise HTTPException(409,'麦克风识别无法启动')
    return {'started':True,'mode':state['mic_mode'],'gain':state['gain'],'full_duplex':state['playing']}

@app.post('/stop')
def stop_capture():
    wakeword.invalidate(cooldown=True)
    capture_stop.set()
    return {'stopping':True}

@app.post('/voice/start')
def start_voice_turn():
    if not state.get('board_connected'):raise HTTPException(409,'板子当前未连接')
    if not launch_capture(VoiceTurn.maximum_seconds,instruction=True):
        raise HTTPException(409,'语音识别尚未就绪或已有识别会话')
    return {'started':True,'mode':'instruction','wait_seconds':VoiceTurn.wait_seconds}

@app.get('/v1/voice/events')
async def stream_voice_events(request:Request,after:str|None=None):
    cursor=after or request.headers.get('last-event-id') or voice_events.cursor()
    try:voice_events.parse(cursor)
    except ValueError as exc:raise HTTPException(400,str(exc)) from None
    async def generate():
        position=cursor;heartbeat=time.monotonic()
        yield voice_events.encode({'id':cursor,'type':'stream.ready','time':time.time(),'data':{}})
        while not await request.is_disconnected():
            batch,position=voice_events.read_after(position)
            for event in batch:yield voice_events.encode(event)
            if time.monotonic()-heartbeat>=15:
                yield ': keepalive\n\n';heartbeat=time.monotonic()
            await asyncio.sleep(.1)
    return StreamingResponse(generate(),media_type='text/event-stream',headers={
        'Cache-Control':'no-cache','X-Accel-Buffering':'no','X-Voice-Event-Cursor':cursor,
    })



@app.post('/device/reboot')
def reboot_device():
    if not state['board_connected']:raise HTTPException(409,'板子当前未连接')
    device_commands.put(('reboot',None))
    return {'rebooting':True}

@app.get('/v1/audio/input')
async def stream_audio_input():
    async def generate():
        subscriber=mic_broadcaster.subscribe(asyncio.get_running_loop());target=subscriber[1]
        try:
            while True:yield await target.get()
        finally:mic_broadcaster.unsubscribe(subscriber)
    return StreamingResponse(generate(),media_type='application/octet-stream',headers={
        'X-Audio-Format':'s16le','X-Audio-Sample-Rate':str(MIC_RATE),'X-Audio-Channels':'2',
    })



@app.get('/recordings/{name}')
def recording(name:str):
    if Path(name).name!=name or not name.endswith('.wav'):raise HTTPException(400)
    path=RECORDINGS/name
    if not path.is_file():raise HTTPException(404)
    return FileResponse(path,media_type='audio/wav')

if __name__=='__main__':
    uvicorn.run(app,host='127.0.0.1',port=7865,log_level='warning')
