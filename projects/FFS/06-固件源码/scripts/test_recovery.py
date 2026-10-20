"""Host regression: actual collector/SCD40 code, simulated clock and hardware.

Requires a native C++ compiler. --zig points to zig.exe (uses `zig c++`).
The generated fragments are exact functions from sensors.cpp, not model copies.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
parser.add_argument('--scenario', action='append')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root / 'src/sensors.cpp').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

with tempfile.TemporaryDirectory(prefix='ffs-host-') as tmp:
    tmp = Path(tmp)
    (tmp / 'scd40_under_test.inc').write_text(function('static SensorReadStatus readScd40('))
    power = function('bool setSensorPower(') if 'bool setSensorPower(' in source else ''
    (tmp / 'power_under_test.inc').write_text(power)
    exe = tmp / 'recovery-test.exe'
    subprocess.run([args.zig, 'c++', '-std=c++17', '-O0',
                    '-I' + str(root / 'tests/host'), '-I' + str(root / 'src'),
                    '-I' + str(tmp), str(root / 'tests/host/recovery_test.cpp'),
                    '-o', str(exe)], check=True)
    for scenario in args.scenario or ['healthy', 'zero_co2', 'zero_burst', 'off_sampling', 'stop_during_off',
                                     'stuck', 'read_errors', 'persistent',
                                     'restore_failure', 'wrap', 'long_uptime', 'stop',
                                     'power_bits', 'ch0', 'ch1', 'ch2', 'ch3', 'ch4',
                                     'display_snapshot']:
        subprocess.run([str(exe), scenario], check=True)
