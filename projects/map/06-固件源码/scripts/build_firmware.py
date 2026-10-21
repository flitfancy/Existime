"""Build normal/all-on firmware for map hardware charge/battery without flashing a board."""
import argparse,hashlib,json,shutil,re
from pathlib import Path
import build as accepted
ROOT=Path(__file__).resolve().parents[1]
NORMAL_SOURCES='main.c app.c compass.c calibration.c charger.c display.c leds.c sensors.c settings.c storage.c platform.c adc.c battery.c startup.S'.split()
FLAGS=['-target','thumb-freestanding-eabi','-mcpu=cortex_m0plus','-Oz','-g','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin','-fno-stack-protector','-ffunction-sections','-fdata-sections','-nostdlib','-Wl,-T,linker.ld','-Wl,-e,Reset_Handler','-Wl,--gc-sections']
def compile_image(cc,kind):
    name='map-app' if kind=='normal' else 'all-led-on'
    sources=NORMAL_SOURCES if kind=='normal' else ['main.c','startup.S']
    accepted.run(cc+FLAGS+sources+['-o',name+'.elf'])
    accepted.run([accepted.sys.executable,'make_binary.py'])
    image=accepted.FW/(name+'.bin');return image,hashlib.sha256(image.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=['all','test','firmware'],nargs='?',default='all')
    p.add_argument('--hardware',choices=['charge','battery','all'],default='all')
    p.add_argument('--mode',choices=['normal','all-on','all'],default='all')
    a=p.parse_args();versions=['charge','battery'] if a.hardware=='all' else [a.hardware]
    modes=['normal','all-on'] if a.mode=='all' else [a.mode];cc=accepted.compiler();results=[]
    # Both evidence-bound generators validate actual LED wiring and pin maps.
    for version in ['charge','battery']:
        accepted.FW=ROOT/'firmware'/version;accepted.generate()
    assert (ROOT/'firmware/charge/board_generated.h').read_bytes()==(ROOT/'firmware/battery/board_generated.h').read_bytes(),'LED mapping changed between hardware revisions'
    for version in versions:
        for mode in modes:
            accepted.FW=ROOT/'firmware'/(version if mode=='normal' else 'all-on')
            record={'hardware':version,'mode':mode,'physicalValidation':'accepted on charge' if version=='charge' else 'prototype; not yet tested on battery hardware'}
            if mode=='normal':record['applicationVersion']=int(re.search(r'#define MAP_APPLICATION_VERSION (\d+)',(accepted.FW/'firmware.h').read_text()).group(1))
            if a.action in ['test','all'] and mode=='normal':
                accepted.test(cc);record['hostTests']=json.loads((accepted.FW/'host-verification.json').read_text())
            if a.action in ['firmware','all']:
                image,digest=compile_image(cc,mode)
                destination=ROOT/'releases'/version;destination.mkdir(parents=True,exist_ok=True)
                if version=='charge' and mode=='normal':
                    expected=json.loads((destination/'manifest.json').read_text())
                    assert digest==expected['sha256'],'charge normal differs from accepted v5 image'
                if mode=='all-on':
                    expected=json.loads((ROOT/'firmware/all-on/charge-physical-acceptance.json').read_text())
                    assert digest==expected['firmware_sha256'],'All-on differs from accepted charge all-LED test'
                shutil.copy2(image,destination/image.name)
                record.update(image=image.name,bytes=image.stat().st_size,sha256=digest,flashAddress='0x08000000',ledMappingMatchesBothHardwareVersions=True)
                if version=='battery' and mode=='normal':
                    record.update(batteryProfile='2x alkaline AA/AAA; 1.8-3.2V linear eight-bar voltage indication',chargerStatusSupported=False)
                    (destination/'manifest.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
                if mode=='all-on':(destination/'all-led-on.manifest.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
            results.append(record)
    if a.action=='all' and a.hardware=='all' and a.mode=='all':
        (ROOT/'releases/MANIFEST.json').write_text(json.dumps({'toolchain':'ziglang==0.13.0','images':results},indent=2)+'\n',encoding='utf-8')
    print(json.dumps(results,indent=2))
if __name__=='__main__':main()
