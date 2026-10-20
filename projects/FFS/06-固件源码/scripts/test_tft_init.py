"""Exercise the real ST7735 initR/initB -> commonInit frequency call chain."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
parser.add_argument('--baseline', action='store_true', help='Reproduce the previous begin() default-rate call')
parser.add_argument('--baseline-offsets', action='store_true', help='Reproduce retained GREEN offsets when switching profiles')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
library = root / 'lib/Firefly_ST7735'


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


base = function((library / 'Adafruit_ST77xx.cpp').read_text(), 'void Adafruit_ST77xx::commonInit(')
if args.baseline:
    base = base.replace('begin(_initSpiFrequency);', 'begin();')
source = (library / 'Adafruit_ST7735.cpp').read_text()
init = function(source, 'void Adafruit_ST7735::initR(') + '\n' + function(source, 'void Adafruit_ST7735::initB(')
if args.baseline_offsets:
    init = init.replace('  _colstart = _rowstart = 0;\n', '')
header = (library / 'Adafruit_ST7735.h').read_text()
defines = '\n'.join(re.findall(r'^#define INITR_.*$', header, re.M))
base_header = (library / 'Adafruit_ST77xx.h').read_text()
defines += '\n' + '\n'.join(re.findall(r'^#define ST7735_TFT(?:WIDTH|HEIGHT)_.*$', base_header, re.M))
tables = sorted(set(re.findall(r'\b(?:Bcmd|Rcmd\w*)\b', init)))
code = r'''
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>
'''+defines+'\n'+'\n'.join('static const uint8_t '+n+'[] = {0};' for n in tables)+r'''
constexpr uint8_t ST77XX_MADCTL = 0x36;
constexpr uint8_t ST77XX_INVOFF = 0x20, ST77XX_INVON = 0x21;
class Adafruit_ST77xx {
public:
  uint32_t _initSpiFrequency = 0, actualFrequency = 0;
  uint8_t invertOnCommand = 0, invertOffCommand = 0;
  unsigned _width = 128, _height = 160, _colstart = 0, _rowstart = 0;
  std::vector<uint32_t> commandFrequencies;
  void setInitSPISpeed(uint32_t hz) { _initSpiFrequency = hz; }
  void begin(uint32_t hz = 0) { actualFrequency = hz ? hz : 32000000; }
  void displayInit(const uint8_t*) { commandFrequencies.push_back(actualFrequency); }
  void sendCommand(uint8_t, const uint8_t*, unsigned) { commandFrequencies.push_back(actualFrequency); }
  void setRotation(uint8_t) { commandFrequencies.push_back(actualFrequency); }
  void commonInit(const uint8_t*);
};
class Adafruit_ST7735 : public Adafruit_ST77xx {
public:
  uint8_t tabcolor = 0;
  void initR(uint8_t);
  void initB();
};
'''+base+'\n'+init+r'''
int main() {
  for (uint32_t requested : {1000000u, 8000000u, 0u}) {
    for (int profile : {int(INITR_GREENTAB), int(INITR_REDTAB), int(INITR_BLACKTAB), -1}) {
      Adafruit_ST7735 display;
      display.setInitSPISpeed(requested);
      if (profile < 0) display.initB(); else display.initR(profile);
      const uint32_t expected = requested ? requested : 32000000;
      if (display.commandFrequencies.empty()) return 2;
      for (uint32_t hz : display.commandFrequencies) if (hz != expected) {
        std::cerr << "FAIL: init command clock " << hz << " expected " << expected << '\n';
        return 1;
      }
    }
  }
  Adafruit_ST7735 switching;
  switching.initR(INITR_GREENTAB);
  switching.initR(INITR_BLACKTAB);
  if (switching._colstart != 0 || switching._rowstart != 0) {
    std::cerr << "FAIL: GREEN->BLACK retained offsets " << switching._colstart
              << ',' << switching._rowstart << '\n';
    return 1;
  }
  switching.initR(INITR_GREENTAB);
  switching.initB();
  if (switching._colstart != 0 || switching._rowstart != 0) return 3;
  std::cout << "PASS: requested SPI init speed and clean offsets across GREEN/BLACK/B reinitialization\n";
}
'''
with tempfile.TemporaryDirectory(prefix='ffs-tft-init-') as directory:
    directory = Path(directory)
    source = directory / 'test.cpp'
    source.write_text(code)
    exe = directory / 'test.exe'
    subprocess.run([args.zig, 'c++', '-std=c++17', str(source), '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)])
    raise SystemExit(result.returncode)
