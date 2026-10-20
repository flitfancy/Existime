"""Capture gain must help weak input without clipping or pumping the AEC path."""
from unittest.mock import patch
import numpy as np
from aec_processor import EchoCanceller


class Passthrough:
    def __init__(self,**kwargs):self.input=None
    def set_stream_format(self,*args):pass
    def set_reverse_stream_format(self,*args):pass
    def set_stream_delay(self,*args):pass
    def process_reverse_stream(self,body):self.reverse=body
    def process_stream(self,body):
        self.input=np.frombuffer(body,dtype='<i2').copy()
        return body


def main():
    with patch('aec_audio_processing.AudioProcessor',Passthrough):
        engine=EchoCanceller(2,50)
        weak=np.full((160,2),100,np.int16);reference=np.full(160,1200,np.int16)
        output=engine.process(weak,reference)
        assert engine.processors[0].input[0]==1600, 'weak capture did not reach nominal AEC level'
        assert np.array_equal(output,weak), 'gain leaked into output level'
        assert engine.processors[0].reverse==reference.astype('<i2').tobytes(), 'render reference was amplified'
        loud=weak.copy();loud[:,0]=25000
        output=engine.process(loud,reference)
        assert np.max(engine.processors[0].input)<=26215, 'new capture clipping'
        assert np.max(np.abs(output.astype(np.int32)-loud))<=1
        assert engine.processors[1].input[0]==1600, 'channels shared capture gain'
        engine.process(weak,reference)
        assert engine.processors[0].input[0]<200, 'gain pumped back up inside an adapted filter'
        extremes=np.tile(np.array([[-32768,32767]],np.int16),(160,1))
        assert np.array_equal(engine.process(extremes,reference),extremes), 'full-scale input corrupted'
    print('weak input scaling, unchanged output/render, independent headroom and full scale passed')


if __name__=='__main__':main()
