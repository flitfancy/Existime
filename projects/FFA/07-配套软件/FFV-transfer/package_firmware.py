"""Package this PC's application binary; output remains in ignored private storage."""
import json
from pathlib import Path
from ota_service import inspect_firmware

ROOT = Path(__file__).resolve().parent


def main():
    data = (ROOT.parents[1]/'06-固件源码'/'build-wifi-transport'/'firmware-wifi-transport.ino.bin').read_bytes()
    metadata = inspect_firmware(data)
    destination = ROOT/'media-work/ota'/metadata['version']
    destination.mkdir(parents=True, exist_ok=True)
    path = destination/f"firefly-voice-{metadata['version']}.bin"
    path.write_bytes(data)
    metadata['file'] = path.name
    (destination/'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    print(path)
    print(json.dumps(metadata))


if __name__ == '__main__': main()
