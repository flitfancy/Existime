"""Exercise actual page/cache mapping, selector and public heart snapshot getter."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--zig',required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
source=(root/'src/heart_rate.cpp').read_text(encoding='utf-8')
start=source.index('HeartRateDisplayValues heartRateDisplayValues(')
begin=source.index('{',start)
end,depth=begin+1,1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}')
    end+=1
with tempfile.TemporaryDirectory(prefix='ffs-tft-pages-') as directory:
    tmp=Path(directory)
    (tmp/'heart_display_under_test.inc').write_text(source[start:end])
    exe=tmp/'pages.exe'
    subprocess.run([args.zig,'c++','-std=c++17','-O0','-I'+str(root/'tests/host'),
                    '-I'+str(root/'src'),'-I'+str(tmp),str(root/'tests/host/tft_pages_test.cpp'),
                    str(root/'src/tft_pages.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
