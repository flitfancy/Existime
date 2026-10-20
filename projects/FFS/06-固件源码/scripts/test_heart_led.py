"""Run the production LED module with a clock and U5 register fixture."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
updated = 'void ledUpdateHeartRate(' in (root / 'src/leds.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='ffs-heart-led-') as temporary:
    exe = Path(temporary) / 'heart-led-test.exe'
    subprocess.run([args.zig, 'c++', '-std=c++17', '-O0', '-include', 'cmath',
                    '-DHAS_HEART_LED=' + str(int(updated)),
                    '-I' + str(root / 'tests/host/led'), '-I' + str(root / 'tests/host'),
                    '-I' + str(root / 'src'), str(root / 'src/leds.cpp'),
                    str(root / 'tests/host/heart_led_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
if updated and 'ledUpdateHeartRate(heart.valid, heart.bpm)' not in (root / 'src/main.cpp').read_text(encoding='utf-8'):
    raise SystemExit('Main loop does not feed the cached valid heart measurement into D8.')
