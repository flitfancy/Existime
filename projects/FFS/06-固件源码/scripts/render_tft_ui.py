"""Render the actual TFT UI and pinned font/GFX code on the host for visual QA."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--pages', action='store_true', help='Render cached multi-page models and waiting states')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
deps = root/'.ota-build/.pio/libdeps/esp32s3_n16r8_usb'
gfx = deps/'Adafruit GFX Library'
u8 = deps/'U8g2_for_Adafruit_GFX/src'
args.output.mkdir(parents=True, exist_ok=True)

arduino = r'''
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
#define PROGMEM
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#define pgm_read_dword(p) (*(const uint32_t*)(p))
#define pgm_read_ptr(p) (*(const void* const*)(p))
using boolean = bool;
class __FlashStringHelper {};
class String { std::string s; public: const char* c_str() const { return s.c_str(); } size_t length() const { return s.size(); } };
inline void yield() {}
inline float radians(float degrees) { return degrees * 0.017453292519943295f; }
'''
printer = r'''
#pragma once
#include <cstdint>
#include <cstring>
class Print {
public:
 virtual ~Print() = default;
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t* p,size_t n) { size_t done=0; while(n--) done+=write(*p++); return done; }
 size_t print(const char* p) { return write((const uint8_t*)p,strlen(p)); }
};
'''
main = r'''
#include <fstream>
#include <string>
#include "tft_ui.h"
void render(const std::string& name, const char* temp, const char* rh, const char* co2,
            bool valid, bool running, bool connected) {
 GFXcanvas16 screen(160,128), strip(160,kTftUiTemperatureHeight);
 screen.fillScreen(kTftUiBackground);
 tftUiHeader(screen, connected);
 tftUiTemperature(strip,temp,valid);
 screen.drawRGBBitmap(0,kTftUiTemperatureY,strip.getBuffer(),160,kTftUiTemperatureHeight);
 tftUiSecondary(strip,rh,valid,co2,valid);
 screen.drawRGBBitmap(0,kTftUiSecondaryY,strip.getBuffer(),160,kTftUiSecondaryHeight);
 tftUiFooter(screen,running,valid,connected);
 std::ofstream file(name,std::ios::binary);
 file << "P6\n160 128\n255\n";
 for(int i=0;i<160*128;++i) {
   const uint16_t c=screen.getBuffer()[i];
   const char rgb[3]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};
   file.write(rgb,3);
 }
}
int main(int argc,char** argv) {
 const std::string path=std::string(argv[1])+"/";
 render(path+"live.ppm","27.1","57.4","1825",true,true,true);
 render(path+"limits.ppm","125.0","100.0","65535",true,true,false);
 render(path+"negative.ppm","-40.0","0.0","400",true,true,true);
 render(path+"paused.ppm","--","--","--",false,false,false);
}
'''
names = ['live','limits','negative','paused']
additional_sources = []
if args.pages:
    names = ['environment','pressure','light','gas','spectrum','heart','light-limits',
             'gas-warming','spectrum-waiting','heart-stale','heart-contact','paused']
    additional_sources = [str(root/'src/tft_pages.cpp')]
    main = r'''
#include <fstream>
#include <string>
#include "tft_ui.h"
#include "sensors.h"
#include "heart_rate.h"
static SensorValues sensors[6];
static bool available[6]={true,true,true,true,true,true};
static bool running=true;
static HeartRateDisplayValues hr;
bool collectorLatestSensor(uint8_t ch,SensorValues& out) { out=sensors[ch]; return available[ch]&&running; }
HeartRateDisplayValues heartRateDisplayValues() { return hr; }
void render(const std::string& name,TftPage page) {
 GFXcanvas16 screen(160,128), strip(160,kTftUiTemperatureHeight);
 const auto values=tftPageValues(page);
 screen.fillScreen(kTftUiBackground);
 tftUiHeader(screen,true,tftPageTitle(page));
 tftUiPagePrimary(strip,values);
 screen.drawRGBBitmap(0,kTftUiTemperatureY,strip.getBuffer(),160,kTftUiTemperatureHeight);
 tftUiPageSecondary(strip,values);
 screen.drawRGBBitmap(0,kTftUiSecondaryY,strip.getBuffer(),160,kTftUiSecondaryHeight);
 tftUiFooter(screen,running,values.healthy,true,values.footerNote,uint8_t(page)+1,6,page==TftPage::Heart,values.watchConnected);
 std::ofstream file(name,std::ios::binary); file << "P6\n160 128\n255\n";
 for(int i=0;i<160*128;++i) {
   const uint16_t c=screen.getBuffer()[i];
   const char rgb[3]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};file.write(rgb,3);
 }
}
int main(int argc,char** argv) {
 const std::string path=std::string(argv[1])+"/";
 sensors[0].tempC=27.7; sensors[0].rh=58.8;
 sensors[1].pressurePa=101325; sensors[1].tempC=28.1;
 sensors[3].alsRaw=2499; sensors[3].uvRaw=127; sensors[2].flickerHz=100;
 sensors[4].vocIndex=118; sensors[4].noxIndex=5;
 sensors[5].co2Ppm=1853; sensors[5].tempC=29.1; sensors[5].rh=52.4;
 const uint8_t order[8]={0,1,2,3,6,7,8,9}; const uint16_t bands[8]={800,2400,1900,2600,3500,4200,3900,2100};
 for(int i=0;i<8;++i) sensors[2].spectral[order[i]]=bands[i];
 sensors[2].spectral[10]=16600; sensors[2].spectral[11]=7500;
 hr.connected=hr.valid=hr.hasSample=true; hr.bpm=72; hr.contact=1; hr.ageMs=2000;
 const char* keys[]={"environment","pressure","light","gas","spectrum","heart"};
 for(uint8_t i=0;i<6;++i) render(path+keys[i]+".ppm",TftPage(i));
 sensors[3].alsRaw=16777215; sensors[3].uvRaw=1048575; sensors[2].flickerHz=1;
 render(path+"light-limits.ppm",TftPage::Light);
 sensors[4].vocIndex=sensors[4].noxIndex=0; render(path+"gas-warming.ppm",TftPage::Gas);
 available[2]=false; render(path+"spectrum-waiting.ppm",TftPage::Spectrum);
 hr.valid=false;hr.ageMs=20000; render(path+"heart-stale.ppm",TftPage::Heart);
 hr.contact=0;render(path+"heart-contact.ppm",TftPage::Heart);
 running=false;render(path+"paused.ppm",TftPage::Environment);
}
'''

font_source = (u8/'u8g2_fonts.c').read_text(encoding='latin-1')
subset = '#include <U8g2_for_Adafruit_GFX.h>\n'
for name in ('wqy12_t_gb2312', 'logisoso28_tn', 'logisoso24_tn', 'logisoso20_tn',
             'logisoso16_tn', 'helvB12_tn', 'helvB10_tn', '4x6_tf'):
    start = font_source.index('const uint8_t u8g2_font_'+name+'[')
    end = font_source.index(';\n', start)+1
    subset += font_source[start:end]+'\n'
with tempfile.TemporaryDirectory(prefix='ffs-tft-render-') as directory:
    tmp = Path(directory)
    (tmp/'Arduino.h').write_text(arduino)
    (tmp/'Print.h').write_text(printer)
    for name in ('Adafruit_I2CDevice.h','Adafruit_SPIDevice.h'):
        (tmp/name).write_text('#pragma once\n')
    (tmp/'fonts.cpp').write_bytes(subset.encode('latin-1'))
    (tmp/'main.cpp').write_text(main)
    exe = tmp/'render.exe'
    subprocess.run([args.zig,'c++','-std=c++17','-DARDUINO=10800',
                    '-I'+str(tmp),'-I'+str(root/'src'),'-I'+str(gfx),'-I'+str(u8),
                    str(tmp/'main.cpp'),str(root/'src/tft_ui.cpp'),str(gfx/'Adafruit_GFX.cpp'),
                    str(u8/'U8g2_for_Adafruit_GFX.cpp'),str(tmp/'fonts.cpp'),*additional_sources,'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(tmp)],check=True)
    for name in names:
        with Image.open(tmp/(name+'.ppm')) as img:
            img.save(args.output/(name+'.png'))
            img.resize((800,640),Image.Resampling.NEAREST).save(args.output/(name+'-5x.png'))
if args.pages:
    sheet=Image.new('RGB',(1440,384*((len(names)+2)//3)),(0,0,0))
    for i,name in enumerate(names):
        with Image.open(args.output/(name+'.png')) as img:
            sheet.paste(img.resize((480,384),Image.Resampling.NEAREST),((i%3)*480,(i//3)*384))
    sheet.save(args.output/'all-pages.png')
print('Rendered production GFX/U8g2 UI:',', '.join(names))
