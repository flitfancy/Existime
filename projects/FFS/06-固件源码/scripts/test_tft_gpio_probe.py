"""Verify the production GPIO serializer with real, unchanged GREEN tables."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
library = root / 'lib/Firefly_ST7735'
source = (library / 'Adafruit_ST7735.cpp').read_text()
defines = []
for name in ('Adafruit_ST7735.h', 'Adafruit_ST77xx.h'):
    for line in (library / name).read_text().splitlines():
        if re.match(r'#define (?:ST77\w+|ST_CMD_DELAY)\s+(?:0x[0-9A-Fa-f]+|\d+)\b', line):
            defines.append(line)
tables = []
for name in ('Rcmd1', 'Rcmd2green', 'Rcmd3'):
    start = source.index(name+'[]')
    begin = source.index('{', start)
    end = source.index('}', begin)
    tables.append('const uint8_t '+name+'[] = '+source[begin:end+1]+';')
with tempfile.TemporaryDirectory(prefix='ffs-tft-gpio-') as temporary:
    temporary = Path(temporary)
    (temporary/'tft_probe_tables.inc').write_text('\n'.join(defines+tables))
    exe = temporary/'probe-test.exe'
    subprocess.run([args.zig, 'c++', '-std=c++17', '-O0',
                    '-I'+str(root/'src'), '-I'+str(temporary),
                    str(root/'tests/host/tft_gpio_probe_test.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
