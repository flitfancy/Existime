"""Portable build and regression entry for the accepted hardware-v1/application-v5 sources."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / 'firmware/v1'
COMMON = 'app.c compass.c calibration.c charger.c display.c leds.c sensors.c settings.c storage.c battery.c'.split()


def run(command, capture=False):
    result = subprocess.run(command, cwd=FW, text=True, capture_output=capture)
    if result.returncode:
        if capture:
            print(result.stdout, end='')
            print(result.stderr, end='', file=sys.stderr)
        result.check_returncode()
    if capture:
        print(result.stdout, end='')
    return result.stdout if capture else None


def compiler():
    version = run([sys.executable, '-m', 'ziglang', 'version'], capture=True).strip()
    if version != '0.13.0':
        raise SystemExit('Use ziglang==0.13.0 from requirements-build.txt for this v1 snapshot.')
    return [sys.executable, '-m', 'ziglang', 'cc']


def generate():
    run([sys.executable, 'generate_board.py'])
    run([sys.executable, 'generate_runtime.py'])


def test(cc):
    output = FW / ('host-tests.exe' if os.name == 'nt' else 'host-tests')
    report = FW / 'host-verification.json'
    report.unlink(missing_ok=True)
    run(cc + ['-O2', '-Wall', '-Wextra', '-Werror'] + COMMON + ['test_logic.c', '-o', str(output)])
    summary = run([str(output)], capture=True).strip()
    match = re.match(r'^PASS (\d+) assertions:', summary)
    if not match:
        raise SystemExit('Host regression summary missing.')
    report.write_text(json.dumps({'passed': True, 'assertions': int(match[1]), 'summary': summary}, indent=2) + '\n', encoding='utf-8')


def firmware(cc):
    (FW / 'build-verification.json').unlink(missing_ok=True)
    # Preserve the accepted source order and flags; debug information is outside the flash image.
    sources = 'main.c app.c compass.c calibration.c charger.c display.c leds.c sensors.c settings.c storage.c platform.c adc.c battery.c startup.S'.split()
    run(cc + ['-target', 'thumb-freestanding-eabi', '-mcpu=cortex_m0plus', '-Oz', '-g', '-Wall', '-Wextra', '-Werror',
              '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
              '-nostdlib', '-Wl,-T,linker.ld', '-Wl,-e,Reset_Handler', '-Wl,--gc-sections'] + sources + ['-o', 'map-app.elf'])
    run([sys.executable, 'make_binary.py'])
    expected = json.loads((ROOT / 'releases/v1/manifest.json').read_text(encoding='utf-8'))
    digest = hashlib.sha256((FW / 'map-app.bin').read_bytes()).hexdigest()
    if digest != expected['sha256']:
        raise SystemExit('Build differs from the accepted v1 release. Review intentional source changes before flashing.')
    print('Rebuilt flash image matches the accepted v1 binary.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['test', 'firmware', 'all'], nargs='?', default='all')
    args = parser.parse_args()
    cc = compiler()
    generate()
    if args.action in ('test', 'all'):
        test(cc)
    if args.action in ('firmware', 'all'):
        firmware(cc)


if __name__ == '__main__':
    main()
