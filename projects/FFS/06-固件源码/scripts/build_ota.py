"""Build in a credential-free staging directory, retaining the board's EEPROM configuration."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--pio', default='pio')
parser.add_argument('--pio-python', help='Python interpreter with PlatformIO installed; invokes python -m platformio.')
parser.add_argument('--stage', type=Path)
parser.add_argument('--output-dir', type=Path, help='Override artifact output directory.')
parser.add_argument('--safe-esptool', action='store_true',
                    help='Use system Python dependencies when the bundled _contrib directory is unreadable.')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
stage = (args.stage or root / '.ota-build').resolve()
if stage == root or root in stage.parents and stage != root / '.ota-build':
    raise SystemExit('Use .ota-build or a separate staging directory.')
stage.mkdir(parents=True, exist_ok=True)
shutil.copytree(root / 'src', stage / 'src', dirs_exist_ok=True)
if (root / 'lib').exists():
    shutil.copytree(root / 'lib', stage / 'lib', dirs_exist_ok=True)
shutil.copy2(root / 'platformio.ini', stage / 'platformio.ini')
if args.safe_esptool:
    shutil.copy2(root / 'scripts/esptool_safe.py', stage / 'esptool_safe.py')
    (stage / 'ota_toolchain.py').write_text(
        'Import("env")\nfrom os.path import join\n'
        'env.Replace(OBJCOPY=join(env.subst("$PROJECT_DIR"), "esptool_safe.py"))\n', encoding='utf-8')
    with (stage / 'platformio.ini').open('a', encoding='utf-8') as configuration:
        configuration.write('\n[env]\nextra_scripts = post:ota_toolchain.py\n')
(stage / 'src/wifi_config.h').write_text(
    '#pragma once\n#define WIFI_SSID ""\n#define WIFI_PASSWORD ""\n#define WIFI_RETRY_SECONDS 5\n',
    encoding='utf-8')
pio_command = [args.pio_python, '-m', 'platformio'] if args.pio_python else [args.pio]
subprocess.run(pio_command + ['run', '-d', str(stage), '-e', 'esp32s3_n16r8_usb'], check=True)
image = stage / '.pio/build/esp32s3_n16r8_usb/firmware.bin'
data = image.read_bytes()
marker = b'FIREFLY-SENSE-N16R8-OTA1'.ljust(32, b'\0')
if data[288:320] != marker or data[12:14] != b'\x09\0' or data[23] != 1:
    raise SystemExit('Image descriptor validation failed; do not flash.')
if hashlib.sha256(data[:-32]).digest() != data[-32:]:
    raise SystemExit('Native ESP image digest invalid; do not flash.')
private_wifi = root / 'src/wifi_config.h'
if private_wifi.exists():
    configuration = private_wifi.read_text(encoding='utf-8')
    for key in ('WIFI_SSID', 'WIFI_PASSWORD'):
        match = re.search(r'#define\s+' + key + r'\s+"([^"]*)"', configuration)
        if match and match.group(1) and match.group(1).encode('utf-8') in data:
            raise SystemExit('Configured Wi-Fi default found in image; do not publish or flash.')
version = re.search(r'kFirmwareVersion\[\] = "([^"]+)"',
                    (root / 'src/config.h').read_text(encoding='utf-8')).group(1)
artifacts = (args.output_dir or root / 'artifacts').resolve()
artifacts.mkdir(parents=True, exist_ok=True)
destination = artifacts / ('firefly-sense-' + version + '-ota.bin')
shutil.copy2(image, destination)
print(json.dumps({'application': str(destination), 'bytes': len(data),
                  'sha256': hashlib.sha256(data).hexdigest(), 'descriptor_valid': True,
                  'native_digest_valid': True, 'compiled_wifi_credentials_absent': True}))
