"""Program an explicitly selected CMSIS-DAP probe and verify the STM32C011 target."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--openocd', default='openocd', help='OpenOCD executable with STM32C0 support')
    parser.add_argument('--scripts', type=Path, help='OpenOCD scripts directory, if not found automatically')
    parser.add_argument('--serial', required=True, help='Serial of your CMSIS-DAP probe')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_.:-]+', args.serial):
        parser.error('Probe serial must contain letters, digits, or _.:-')
    image = ROOT / 'releases/charge/map-app.bin'
    manifest = json.loads((image.parent / 'manifest.json').read_text(encoding='utf-8'))
    if hashlib.sha256(image.read_bytes()).hexdigest() != manifest['sha256']:
        raise SystemExit('Release image hash mismatch.')
    image_tcl = image.as_posix().replace('}', '\\}')
    commands = [
        f'adapter serial {args.serial}', 'init',
        'if {([mrw 0x40015800] & 0xfff) != 0x443} {error "Target is not STM32C011"}',
        'if {([mrw 0x1fff75a0] & 0xffff) != 16} {error "Target Flash is not 16 KiB"}',
        'halt', 'wait_halt 2000', 'flash probe 0',
        f'flash write_image erase {{{image_tcl}}} 0x08000000',
        f'verify_image {{{image_tcl}}} 0x08000000',
        'mmw 0x40022000 0 0x10000', 'reset run', 'shutdown'
    ]
    command = [args.openocd]
    if args.scripts:
        command += ['-s', str(args.scripts)]
    command += ['-f', str(ROOT / 'scripts/cmsis-dap.cfg'), '-c', '; '.join(commands)]
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
