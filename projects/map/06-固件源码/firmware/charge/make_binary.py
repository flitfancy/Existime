"""Extract and validate the Cortex-M0+ flash image using ELF32 load segments."""
import json
import struct
import hashlib
import re
from pathlib import Path

root = Path(__file__).resolve().parent
elf = (root/'map-app.elf').read_bytes()
h = struct.unpack_from('<16sHHIIIIIHHHHHH', elf)
assert h[0][:6] == b'\x7fELF\x01\x01' and h[2] == 40, 'Expected little-endian ELF32 ARM'
segments = []
ram_end = 0x20000000
for i in range(h[10]):
    typ, offset, virtual, physical, size, memory, flags, align = struct.unpack_from('<IIIIIIII', elf, h[5]+i*h[9])
    if typ == 1 and 0x20000000 <= virtual < 0x20001800:
        ram_end = max(ram_end, virtual+memory)
    if typ == 1 and size:
        assert 0x08000000 <= physical < 0x08004000
        assert physical+size <= 0x08004000, 'Image exceeds the fitted 16 KiB Flash'
        segments.append((physical, elf[offset:offset+size]))
end = max(a+len(b) for a,b in segments)
image = bytearray(b'\xff'*(end-0x08000000))
for address,data in segments:
    image[address-0x08000000:address-0x08000000+len(data)] = data
stack, reset = struct.unpack_from('<II', image)
assert stack == 0x20001800, 'Wrong stack for 6 KiB RAM'
assert reset & 1 and 0x08000000 <= reset-1 < end, 'Invalid Thumb reset vector'
assert h[4] == reset, 'ELF entry must match the reset vector'
(root/'map-app.bin').write_bytes(image)
version=int(re.search(r'#define MAP_APPLICATION_VERSION (\d+)',(root/'firmware.h').read_text()).group(1))
result={'application_version':version,'flash_image_bytes':len(image),'flash_sha256':hashlib.sha256(image).hexdigest(),
        'stack':hex(stack),'reset':hex(reset),'flash_limit':16384,'allocated_ram_bytes':ram_end-0x20000000,
        'remaining_ram_bytes':stack-ram_end}
assert result['remaining_ram_bytes'] >= 1536, 'Insufficient stack space'
(root/'build-verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result))

