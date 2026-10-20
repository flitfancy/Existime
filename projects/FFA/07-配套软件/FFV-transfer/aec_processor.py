"""Local WebRTC AEC adapter; callers supply time-aligned 16 kHz PCM16 frames.

No transport ownership, muting, noise suppression, or VAD here.
One processor per microphone avoids mixing the channels' acoustic paths.
Internal capture scaling gives weak PCM sufficient level for native AEC; the
inverse scale preserves output level. It never changes saved raw microphone PCM.
"""
import numpy as np


class CaptureScale:
    """A bounded gain per native-filter lifetime, with independent headroom.

    Gain can only decrease when a louder input requires it. It does not rise and
    fall with each frame or normalize silence, which would modulate the echo path.
    A new native filter starts a fresh calibration. Output is inverse-scaled.
    """
    def __init__(self, channels, maximum=16.):
        if not 1 <= maximum <= 32:
            raise ValueError('capture scale must be within 1..32')
        self.gains = np.full(channels, float(maximum))

    def encode(self, microphones):
        values = microphones.astype(np.float64)
        peaks = np.max(np.abs(values), axis=0)
        ceiling = np.maximum(1., .8*32767/np.maximum(peaks, 1.))
        self.gains = np.minimum(self.gains, ceiling)
        return np.clip(np.rint(values*self.gains),-32768,32767).astype(np.int16)

    def decode(self, output):
        return np.clip(np.rint(output.astype(np.float64)/self.gains),-32768,32767).astype(np.int16)


class EchoCanceller:
    rate = 16000
    frame_samples = 160

    def __init__(self, channels=2, delay_ms=50, max_capture_gain=16.):
        from aec_audio_processing import AudioProcessor
        if channels not in (1, 2) or not 0 <= delay_ms <= 500:
            raise ValueError('AEC requires one/two channels and 0..500 ms reference delay')
        self.channels = channels
        self.delay_ms = int(delay_ms)
        self.capture_scale = CaptureScale(channels, max_capture_gain)
        self.processors = []
        for _ in range(channels):
            processor = AudioProcessor(enable_aec=True, enable_ns=False,
                                       enable_agc=False, enable_vad=False)
            processor.set_stream_format(self.rate, 1)
            processor.set_reverse_stream_format(self.rate, 1)
            self.processors.append(processor)

    def process(self, microphones, reference):
        microphones = np.asarray(microphones)
        reference = np.asarray(reference)
        if microphones.shape != (160, self.channels) or reference.shape != (160,):
            raise ValueError('AEC requires exactly 10 ms of microphone and mono reference PCM')
        if microphones.dtype != np.int16 or reference.dtype != np.int16:
            raise ValueError('AEC input must be PCM16, without per-block normalization')
        microphones = self.capture_scale.encode(microphones)
        reverse = reference.astype('<i2', copy=False).tobytes()
        output = np.empty_like(microphones)
        for channel, processor in enumerate(self.processors):
            processor.process_reverse_stream(reverse)
            processor.set_stream_delay(self.delay_ms)
            result = processor.process_stream(microphones[:, channel].astype('<i2').tobytes())
            if len(result) != 320:
                raise RuntimeError('AEC returned an invalid frame length')
            output[:, channel] = np.frombuffer(result, dtype='<i2')
        return self.capture_scale.decode(output)
