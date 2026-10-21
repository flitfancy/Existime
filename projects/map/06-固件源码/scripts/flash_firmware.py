"""Flash an explicitly selected hardware revision and application; verify readback."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hardware',choices=['v1','v2'],required=True)
    p.add_argument('--mode',choices=['normal','all-on'],required=True)
    p.add_argument('--serial',required=True,help='CMSIS-DAP probe serial')
    p.add_argument('--openocd',default='openocd')
    p.add_argument('--scripts',type=Path)
    a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.:-]+',a.serial):p.error('Invalid probe serial')
    folder=ROOT/'releases'/a.hardware
    name='map-app.bin' if a.mode=='normal' else 'all-led-on.bin'
    manifest=folder/('manifest.json' if a.mode=='normal' else 'all-led-on.manifest.json')
    image=folder/name;record=json.loads(manifest.read_text(encoding='utf-8'))
    if hashlib.sha256(image.read_bytes()).hexdigest()!=record['sha256']:raise SystemExit('Firmware checksum mismatch')
    image_tcl=image.as_posix().replace('}', '\\}')
    commands=[f'adapter serial {a.serial}','init',
      'if {([mrw 0x40015800] & 0xfff) != 0x443} {error "Target is not STM32C011"}',
      'if {([mrw 0x1fff75a0] & 0xffff) != 16} {error "Target Flash is not 16 KiB"}',
      'halt','wait_halt 2000','flash probe 0',
      f'flash write_image erase {{{image_tcl}}} 0x08000000',
      f'verify_image {{{image_tcl}}} 0x08000000','mmw 0x40022000 0 0x10000','reset run','shutdown']
    command=[a.openocd]
    if a.scripts:command+=['-s',str(a.scripts)]
    command+=['-f',str(ROOT/'scripts/cmsis-dap.cfg'),'-c','; '.join(commands)]
    print(f'Flashing {a.hardware} {a.mode}: {name}; external EEPROM is preserved')
    subprocess.run(command,check=True)
if __name__=='__main__':main()
