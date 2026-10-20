"""Exercise the production phone BLE protocol/state on the host; never connect to hardware."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--zig', required=True)
parser.add_argument('--fixture', type=Path, help='Save the production-encoded schema-5 fixture for the Android interoperability test.')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='ffs-phone-ble-') as temporary:
    executable = Path(temporary) / 'phone-ble-test.exe'
    fixture = Path(temporary) / 'fixture.jsonl'
    subprocess.run([args.zig, 'c++', '-std=c++11', '-O0', '-I' + str(root / 'src'),
                    str(root / 'tests/host/phone_ble_test.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable), str(fixture)], check=True)
    records = [json.loads(line) for line in fixture.read_text(encoding='utf-8').splitlines()]
    assert [item['type'] for item in records] == ['begin', 'row', 'end']
    assert all(item['v'] == 1 and item['request_id'] == 7 for item in records)
    assert records[0]['rows'] == records[2]['rows'] == 1
    cells = records[1]['csv'].split(',')
    assert len(cells) == 41 and cells[4] == 'CH6 "BLE" \\ 心率' and cells[37] == '76'
    if args.fixture:
        args.fixture.parent.mkdir(parents=True, exist_ok=True)
        args.fixture.write_bytes(fixture.read_bytes())
    print('Phone BLE firmware JSON fixtures decoded and matched protocol 1')
