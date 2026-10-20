"""Compile and exercise the production OTA session without touching the board."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='ffs-ota-test-') as temporary:
    executable = Path(temporary) / 'ota-test.exe'
    subprocess.run([args.zig, 'c++', '-std=c++17', '-O0', '-I' + str(root / 'src'),
                    str(root / 'tests/host/ota_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
