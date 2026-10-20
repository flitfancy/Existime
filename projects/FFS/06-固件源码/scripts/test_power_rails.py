"""Exercise actual power helpers, including X3 preservation and bench behavior."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (root/'src/sensors.cpp').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    begin = source.index('{', start)
    end, depth = begin+1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

has_setup = 'bool powerAccessorySetup(' in source
signatures = ['bool powerAllOff(', 'bool powerAllOn(', 'bool powerBranchOn(',
              'bool setSensorPower(', 'static bool closeBranch(']
if has_setup:
    signatures.append('bool powerAccessorySetup(')
fragment = '#define HAS_ACCESSORY_SETUP '+str(int(has_setup))+'\n'
fragment += '\n'.join(function(sig) for sig in signatures)
with tempfile.TemporaryDirectory(prefix='ffs-power-rails-') as temporary:
    temporary = Path(temporary)
    (temporary/'power_rails_under_test.inc').write_text(fragment)
    for bench in (False, True):
        exe = temporary/('bench.exe' if bench else 'board.exe')
        flags = ['-DBENCH_SDA=1'] if bench else []
        subprocess.run([args.zig, 'c++', '-std=c++17', '-O0', *flags,
                        '-I'+str(root/'tests/host'), '-I'+str(root/'src'),
                        '-I'+str(temporary), str(root/'tests/host/power_rails_test.cpp'),
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
