"""Exercise the production BLE measurement parser and validity rules on the host."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='ffs-heart-rate-') as temporary:
    executable = Path(temporary) / 'heart-rate-test.exe'
    subprocess.run([args.zig, 'c++', '-std=c++17', '-O0', '-I' + str(root / 'src'),
                    str(root / 'tests/host/heart_rate_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
