"""Upload an application image to the board; use its IP address on the same LAN."""
import argparse
import hashlib
import http.client
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('host', help='board IPv4 address')
parser.add_argument('image', type=Path)
args = parser.parse_args()
connection = http.client.HTTPConnection(args.host, 80, timeout=30)
connection.request('GET', '/device')
response = connection.getresponse()
device = json.loads(response.read())
if response.status != 200 or not device.get('ota_supported'):
    raise SystemExit('Board does not report application OTA support.')
data = args.image.read_bytes()
if not 320 <= len(data) <= device['max_firmware_bytes']:
    raise SystemExit('Image size exceeds device limits.')
if data[288:320] != b'FIREFLY-SENSE-N16R8-OTA1'.ljust(32, b'\0'):
    raise SystemExit('Not a FIREFLY-SENSE production-board OTA image.')
connection.close()
connection = http.client.HTTPConnection(args.host, 80, timeout=90)
connection.request('POST', '/device/firmware', data,
                   {'Content-Type': 'application/octet-stream', 'X-FFS-OTA-Token': device['ota_nonce']})
response = connection.getresponse()
result = json.loads(response.read())
connection.close()
if response.status != 200 or not result.get('ok'):
    raise SystemExit('OTA failed: ' + str(result.get('error', response.status)))
print(json.dumps({'ok': True, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                  'previous_partition': device['boot_partition'], 'rebooting': True}))
