import unittest
import numpy as np
from aec_runtime import RenderReference
from playback_spectrum import PlaybackSpectrum


class SpectrumTests(unittest.TestCase):
    def reference(self, frequency=1000, amplitude=.5):
        ref = RenderReference()
        ref.begin(48000)
        samples = amplitude*np.sin(2*np.pi*frequency*np.arange(96000)/48000)
        ref.append(np.rint(samples*32767).astype('<i2').tobytes())
        ref.progress(24000)
        return ref

    def test_frequency_and_volume(self):
        ref = self.reference()
        meter = PlaybackSpectrum(ref)
        data = meter.snapshot(True)
        self.assertEqual(np.shape(data['frames']), (16, 16))
        expected = np.searchsorted(np.geomspace(50, 7600, 17), 1000)-1
        self.assertEqual(int(np.argmax(data['frames'][0])), expected)
        self.assertLess(max(meter.snapshot(True, volume=10)['frames'][0]), max(data['frames'][0]))
        self.assertEqual(np.max(meter.snapshot(True, volume=0)['frames']), 0)
        self.assertEqual(np.max(PlaybackSpectrum(self.reference(amplitude=0)).snapshot(True)['frames']), 0)

    def test_transport_states_and_no_reference_mutation(self):
        ref = self.reference()
        meter = PlaybackSpectrum(ref)
        before = ref.ring.copy()
        meter.snapshot(True)
        np.testing.assert_array_equal(ref.ring, before)
        self.assertFalse(meter.snapshot(False)['active'])
        self.assertFalse(meter.snapshot(True, paused=True)['active'])
        ref.progress(24000, 4)
        self.assertFalse(meter.snapshot(True)['active'])
        ref.progress(24000, 8)
        self.assertTrue(meter.snapshot(True)['active'])
        ref.updated -= 2
        self.assertFalse(meter.snapshot(True)['active'])
        ref.finish()
        self.assertFalse(meter.snapshot(True)['active'])


if __name__ == '__main__':
    unittest.main()
