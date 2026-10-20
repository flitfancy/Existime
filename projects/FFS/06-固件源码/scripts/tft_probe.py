"""Human-in-the-loop TFT probe, adapted for Windows from the diagnosis template.

SPI has no display readback wire: a human must observe the actual panel. Pass
--observation with the user's reported result, or answer the terminal prompt.
No observation is inferred from a successful HTTP response.
"""
import argparse
import http.client
import json
from pathlib import Path
import sys


def request(host, method, path, body=None, headers=None):
    connection = http.client.HTTPConnection(host, timeout=30)
    try:
        connection.request(method, path, body, headers or {})
        response = connection.getresponse()
        data = json.loads(response.read(65536))
        if response.status != 200:
            raise RuntimeError(f'{path}: HTTP {response.status}')
        return data
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('host')
    parser.add_argument('--command', help='Optional TFT command through the diagnostic endpoint')
    parser.add_argument('--observation', choices=['white', 'black', 'rgb', 'data', 'unknown'])
    parser.add_argument('--expected', choices=['rgb', 'data', 'black'], default='data')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    device = request(args.host, 'GET', '/device')
    if device.get('model') != 'FIREFLY-SENSE N16R8':
        raise RuntimeError('Unexpected device')
    if args.command:
        if not args.command.startswith('TFT'):
            raise RuntimeError('Only TFT commands are allowed')
        request(args.host, 'POST', '/display/command', args.command.encode('ascii'),
                {'Content-Type': 'text/plain', 'X-FFS-OTA-Token': device['ota_nonce']})
    else:
        # Exercise the live main-loop path rather than accepting a cached website.
        request(args.host, 'GET', '/data')
    observation = args.observation or input('Observe the panel: white / black / rgb / data / unknown: ').strip()
    display = None
    if device.get('firmware_version') != '1.3.7':
        display = request(args.host, 'GET', '/display')
    result = {
        'firmware_version': device.get('firmware_version'),
        'boot_partition': device.get('boot_partition'),
        'uptime_ms': device.get('uptime_ms'),
        'command': args.command or 'existing automatic TFT refresh',
        'observation_source': 'human', 'observation': observation,
        'expected': args.expected,
        'display_configuration': display,
        'verdict': 'PENDING' if observation == 'unknown' else
                   'PASS' if observation == args.expected else 'FAIL',
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['verdict'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
