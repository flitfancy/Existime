import math
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

from microphone_history import MicrophoneHistory


class HistoryTests(unittest.TestCase):
    def test_energy_weighting_restart_gaps_and_gain(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'history.sqlite3'
            h = MicrophoneHistory(path)
            h.initialize()
            # Very different levels must be averaged in power, never in decibels.
            h.offer(-20, -40, 240, 24000, 4, 120)
            h.offer(-40, -60, 720, 24000, 4, 121)
            h.flush()
            restarted = MicrophoneHistory(path)
            restarted.initialize()
            restarted.offer(-40, -60, 240, 24000, 8, 123)
            restarted.offer(-50, -50, 240, 24000, 8, 360)
            restarted.flush()
            result = restarted.query('24h', 400)
            self.assertEqual(len(result['rows']), 2)  # No invented data in gap.
            row = result['rows'][0]
            expected = round(10 * math.log10((240 * .01 + 960 * .0001) / 1200), 1)
            self.assertEqual(row['left'], expected)
            self.assertEqual(row['left_max'], -20)
            self.assertEqual(row['gains'], [4, 8])
            self.assertAlmostEqual(row['seconds'], .05)
            self.assertEqual(result['first_recorded'], 120)
            self.assertEqual(len(restarted.query('5m', 400)['rows']), 4)
            # Old history remains available regardless of age.
            self.assertEqual(len(restarted.query('all', 40000000)['rows']), 1)
            self.assertEqual(restarted.query('1h', 40000000)['rows'], [])

    def test_failed_write_retries_without_loss_or_duplication(self):
        with tempfile.TemporaryDirectory() as folder:
            h = MicrophoneHistory(Path(folder) / 'history.sqlite3')
            h.initialize()
            h.offer(-30, -40, 240, 24000, 4, 120)
            with patch('microphone_history.sqlite3.connect', side_effect=sqlite3.OperationalError('disk full')):
                h.flush()
            self.assertIn('disk full', h.error)
            h.offer(-30, -40, 240, 24000, 4, 121)
            h.flush()
            h.flush()
            self.assertFalse(h.error)
            self.assertEqual(sum(row['seconds'] for row in h.query('all', 180)['rows']), .02)

    def test_invalid_ranges(self):
        h = MicrophoneHistory('unused')
        for span, end in [('oops', None), ('all', float('nan')), ('24h', -1)]:
            with self.assertRaises(ValueError):
                h.query(span, end)


if __name__ == '__main__':
    unittest.main()
