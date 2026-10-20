"""Verify STT consumes processed audio and finalized WAVs retain the raw source."""
import tempfile,wave,zlib
from pathlib import Path
import numpy as np
import app

def main():
    with tempfile.TemporaryDirectory() as directory:
        app.RECORDINGS=Path(directory)
        app.state.update(mic_mode='both',gain=4)
        capture=app.MicCapture(10);capture.start()
        rng=np.random.default_rng(20)
        processed=[]
        for seq in range(300):
            raw=rng.integers(-15000,15000,(240,2),dtype=np.int16).tobytes()
            capture.process(('audio',seq,2,zlib.crc32(raw),raw))
            clean=rng.normal(0,.015,(160,2)).astype(np.float32);processed.append(clean)
            capture.process_clean(seq,clean,[.1,.95],True)
        capture.stop();capture.finish()
        job=app.jobs.get_nowait()
        assert job[3]=='right' and job[5]==16000,'wrong recognition channel/rate'
        expected=np.clip(np.rint(np.concatenate(processed)[:,1]*32768),-32768,32767)/32768
        expected-=expected.mean()
        assert np.allclose(job[2],expected,atol=1e-7),'raw microphone leaked into STT'
        with wave.open(str(app.RECORDINGS/app.state['wav'])) as f:
            assert f.getframerate()==16000 and f.getnframes()==48000
        with wave.open(str(app.RECORDINGS/app.state['wav_raw'])) as f:
            assert f.getframerate()==24000 and f.getnframes()==72000
        assert not app.state['recording']
        # Silence according to post-AEC VAD must not be enqueued for STT.
        while not app.jobs.empty():app.jobs.get_nowait()
        quiet=app.MicCapture(10);quiet.first=1000
        for seq in range(1000,1300):quiet.process_clean(seq,np.zeros((160,2),np.float32),[0.,0.],True)
        assert app.jobs.empty()
        # A missing input interval must flush once and never join speech across it.
        gapped=app.MicCapture(10);gapped.first=0
        for seq in list(range(40))+list(range(45,85)):
            samples=rng.normal(0,.015,(160,2)).astype(np.float32)
            gapped.process_clean(seq,samples,[.95,.1],True)
        gapped.finish()
        parts=[]
        while not app.jobs.empty():parts.append(app.jobs.get_nowait())
        assert len(parts)==2 and all(len(job[2])==40*160 for job in parts)
        assert len(gapped.clean)==85*160*4, 'missing clean interval was not padded'
    print('processed STT route, VAD gate, channel selection, raw/clean WAV rates ok')

if __name__=='__main__':main()
