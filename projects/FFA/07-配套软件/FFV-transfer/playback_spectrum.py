"""Small, read-only visual preview of the board's speaker reference timeline.

FFT runs in the HTTP worker, outside the transport and recognition paths.
The existing 16 kHz reference limits the display to frequencies below 8 kHz.
"""
import numpy as np


class PlaybackSpectrum:
    RATE = 16000
    SIZE = 1024
    STEP = 800
    FRAMES = 16
    BANDS = 16

    def __init__(self, reference):
        self.reference = reference
        self.window = np.hanning(self.SIZE)
        edges = np.geomspace(50, 7600, self.BANDS + 1)
        frequencies = np.fft.rfftfreq(self.SIZE, 1 / self.RATE)
        self.bins = [np.flatnonzero((frequencies >= lo) & (frequencies < hi))
                     for lo, hi in zip(edges[:-1], edges[1:])]

    def snapshot(self, playing, paused=False, volume=100):
        idle = {'active': False, 'step_ms': 50, 'frames': []}
        context = self.reference.context()
        if not playing or paused or not context['active'] or not context['running'] or context['stale']:
            return idle
        start = int(context['position']) - self.SIZE // 2
        samples = self.reference.read(start, self.SIZE + self.STEP * (self.FRAMES - 1), context['epoch'])
        if samples is None:
            return idle
        # No mutation of the shared AEC reference. Only small numeric bars leave here.
        frames = np.stack([samples[i*self.STEP:i*self.STEP+self.SIZE] for i in range(self.FRAMES)])
        amplitude = np.abs(np.fft.rfft(frames * self.window, axis=1)) * (2 / self.window.sum())
        amplitude *= max(0, min(100, float(volume))) / 100
        peaks = np.stack([amplitude[:, bins].max(axis=1) for bins in self.bins], axis=1)
        levels = np.clip((20*np.log10(np.maximum(peaks, 1e-8)) + 72) / 72, 0, 1)
        return {'active': True, 'step_ms': 50, 'frames': np.rint(levels*100).astype(int).tolist()}
