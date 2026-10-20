"""Endpoint, long-speech and interruption behavior for real streaming packets."""
import numpy as np
from recognition_segments import UtteranceBuffer


def main():
    voice=np.full((160,2),1000,np.int16).tobytes();quiet=bytes(640)
    buffer=UtteranceBuffer(2,'both')
    for _ in range(600):
        assert buffer.push(voice,[.95,.05],True) is None, 'a continuous sentence was cut'
    for _ in range(79):assert buffer.push(quiet,[.01,.01],False) is None
    pcm,scores,playback=buffer.push(quiet,[.01,.01],False)
    assert len(pcm)==680*640 and scores[0]>scores[1] and playback
    assert buffer.finish() is None, 'sentence emitted twice'
    # Short false positives cannot activate a transcript, and silence is bounded.
    for _ in range(500):buffer.push(quiet,[.01,.01],False)
    for _ in range(3):buffer.push(voice,[.95,.05],False)
    assert buffer.finish() is None
    # Long continuous speech is bounded; stopping flushes a pending utterance.
    count=0
    for _ in range(3100):
        result=buffer.push(voice,[.95,.05],False)
        if result:
            assert len(result[0])<=15*16000*4
            count+=1
    assert count==2 and buffer.finish() is not None
    # Selecting one microphone must not trigger from the other channel.
    right=UtteranceBuffer(1,'right')
    for _ in range(200):assert right.push(bytes(320),[.99,.01],True) is None
    assert right.finish() is None
    print('whole sentences, endpoint hold, bounded silence/long speech, stop and selected channel passed')


if __name__=='__main__':main()
