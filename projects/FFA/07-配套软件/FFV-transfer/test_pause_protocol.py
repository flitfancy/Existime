from unittest.mock import Mock
from output_scheduler import OutputScheduler, PCMSource
from output_session import OutputSession
from protocol_v2 import BufferStatus, Frame, FrameType

class FakeConnection:
    def __init__(self):self.frames=[]
    def send_frame(self,kind,payload=b'',*,flags=0):
        seq=len(self.frames)+1;self.frames.append(Frame(kind,seq,payload,flags));return seq

def main():
    state={}
    scheduler=OutputScheduler(state);scheduler.connected()
    task=scheduler.reserve('file','test')
    scheduler.submit(task,PCMSource(bytes(48000*2*30),48000))
    c=FakeConnection();output=OutputSession(scheduler.take(),scheduler,c,Mock())
    output.handle(Frame(FrameType.BUFFER_STATUS,output.start_sequence,BufferStatus(0,0,0,576000).pack()))
    for _ in range(100):output.drive()
    assert output.sent==288000,'sender must stop at six seconds without consumed-frame telemetry'
    scheduler.pause(True);output.drive()
    assert c.frames[-1].type==FrameType.PAUSE
    before=output.sent;output.drive();assert output.sent==before
    output.handle(Frame(FrameType.PLAY_PROGRESS,99,BufferStatus(288000,12000,276000,576000).pack(),4))
    assert state['play_paused'] and state['play_position']==.25
    scheduler.pause(False);output.drive()
    assert c.frames[-1].type==FrameType.RESUME
    output.handle(Frame(FrameType.PLAY_PROGRESS,100,BufferStatus(288000,12000,276000,576000).pack(),8))
    output.drive();assert output.sent>before and not state['play_paused']
    scheduler.cancel();output.drive();assert c.frames[-1].type==FrameType.STOP
    count=len(c.frames);output.drive();assert len(c.frames)==count,'no audio after STOP'

    print('v2 bounded send-ahead, pause/resume telemetry, and stop ordering ok')

if __name__=='__main__':main()
