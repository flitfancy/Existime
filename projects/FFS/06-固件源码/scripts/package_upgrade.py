"""Package the validated application and an explicit rollback application."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile


def checked_image(path):
    data = path.read_bytes()
    marker = b'FIREFLY-SENSE-N16R8-OTA1'.ljust(32, b'\0')
    if (len(data) < 320 or data[288:320] != marker or data[12:14] != b'\x09\0'
            or data[23] != 1 or hashlib.sha256(data[:-32]).digest() != data[-32:]):
        raise SystemExit(f'Invalid FIREFLY-SENSE application: {path.name}')
    return data, hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rollback-version', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'\d+\.\d+\.\d+', args.rollback_version):
        raise SystemExit('Invalid rollback version')
    root = Path(__file__).resolve().parents[1]
    config = (root / 'src/config.h').read_text(encoding='utf-8')
    version = re.search(r'kFirmwareVersion\[\] = "([^"]+)"', config).group(1)
    artifacts = root / 'artifacts'
    image = artifacts / f'firefly-sense-{version}-ota.bin'
    data, digest = checked_image(image)
    rollback = artifacts / f'firefly-sense-{args.rollback_version}-ota.bin'
    _, rollback_digest = checked_image(rollback)
    manifest = json.loads((root / 'VERSION_MANIFEST.json').read_text(encoding='utf-8'))
    if manifest['firmware_version'] != version:
        raise SystemExit('Manifest version does not match the source')
    metadata = {
        'firmware_version': version, 'schema_version': manifest['schema_version'],
        'board': manifest['board'], 'bytes': len(data), 'sha256': digest,
        'descriptor_valid': True, 'native_digest_valid': True,
        'compiled_wifi_credentials_absent': True,
        'state': manifest['state'], 'hardware_validation_completed': False,
        'display': manifest['display'], 'rollback_version': args.rollback_version,
        'rollback_sha256': rollback_digest,
    }
    # Repeat the credential check rather than relying on the build caller.
    private = root / 'src/wifi_config.h'
    if private.exists():
        text = private.read_text(encoding='utf-8')
        for key in ('WIFI_SSID', 'WIFI_PASSWORD'):
            match = re.search(r'#define\s+' + key + r'\s+"([^\"]*)"', text)
            if match and match.group(1) and match.group(1).encode('utf-8') in data:
                raise SystemExit('Private Wi-Fi default found in application')
    metadata_path = image.with_suffix('.json')
    metadata_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    files = [(image, image.name), (metadata_path, metadata_path.name),
             (root / f'UPGRADE_{version}.md', f'UPGRADE_{version}.md'),
             (rollback, 'rollback/' + rollback.name),
             (artifacts / f'test-results-{version}.json', f'test-results-{version}.json')]
    notices = root / 'FONT_NOTICES.txt'
    if notices.exists():
        files.append((notices, notices.name))
    checksums = {name: hashlib.sha256(path.read_bytes()).hexdigest() for path, name in files}
    destination = artifacts / f'firefly-sense-{version}-upgrade.zip'
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as package:
        for path, name in files:
            package.write(path, name)
        package.writestr('checksums.json', json.dumps(checksums, indent=2) + '\n')
    with zipfile.ZipFile(destination) as package:
        if package.testzip() is not None:
            raise SystemExit('ZIP integrity check failed')
        for name, checksum in checksums.items():
            if hashlib.sha256(package.read(name)).hexdigest() != checksum:
                raise SystemExit('Packaged checksum mismatch: ' + name)
    print(json.dumps({'package': str(destination), 'application_bytes': len(data),
                      'sha256': digest, 'zip_members_verified': len(files),
                      'rollback_version': args.rollback_version}))


if __name__ == '__main__':
    main()
