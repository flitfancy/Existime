import socket,struct,tempfile,threading,time,zlib
from pathlib import Path
from ffv_v2_client import FireflyVoiceConnection
from protocol_v2 import FrameParser,FrameType,encode_frame

def main():
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1)
    done=threading.Event();errors=[]
    def board():
        try:
            with listener.accept()[0] as conn:
                conn.settimeout(3);parser=FrameParser()
                while not done.is_set():
                    chunk=conn.recv(8192)
                    if not chunk:break
                    for frame in parser.feed(chunk):
                        if frame.type==FrameType.AUTH:
                            assert frame.payload==b'K'*32
                            packet=encode_frame(FrameType.AUTH_RESULT,frame.sequence,b'\0')
                        elif frame.type==FrameType.PING:
                            packet=encode_frame(FrameType.PONG,frame.sequence,frame.payload)
                        else:raise AssertionError(frame)
                        for pos in range(0,len(packet),3):conn.sendall(packet[pos:pos+3])
        except Exception as e:
            if not done.is_set():errors.append(e)
    thread=threading.Thread(target=board);thread.start()
    try:
        with tempfile.TemporaryDirectory() as directory:
            key=Path(directory)/'test.key';key.write_bytes(b'K'*32)
            c=FireflyVoiceConnection('127.0.0.1',listener.getsockname()[1],pairing_key_file=key,mic_port=0)
            try:
                seq=c.send_frame(FrameType.PING,b'payload q R B X')
                frames=c.wait_for(lambda f:f.type==FrameType.PONG,timeout=2)
                pong=next(f for f in frames if f.type==FrameType.PONG)
                assert pong.sequence==seq and pong.payload==b'payload q R B X'
                payload=bytes(1920);crc=zlib.crc32(payload)
                with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
                    udp.sendto(struct.pack('<IIHHI',0x41564646,7,1920,14,crc)+payload,c.audio_socket.getsockname())
                deadline=time.monotonic()+1;frame=None
                while frame is None and time.monotonic()<deadline:frame=c.read_audio()
                assert frame==('audio',7,14,crc,payload)
            finally:done.set();c.close()
    finally:thread.join(4);listener.close()
    assert not errors,errors
    assert not thread.is_alive()
    print('v2 socket authentication, fragmented replies, persistent ping, UDP input ok')

if __name__=='__main__':main()
