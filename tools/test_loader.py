"""Exercise the binary loader's acceptance and rejection boundaries."""
import struct
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
exe = root/'outputs/second-son-loader-check.exe'
header = struct.pack('<8s6Q', b'BBPROBE5', 8192, 0, 2, 2, 1, 0)
metadata = struct.pack('<7Q', 4096, 0, 0, 0, 0, 0, 0)
segments = struct.pack('<6Q', 0, 4096, 5, 4096, 4096, 6)
name = b'unknown#import'.ljust(128, b'\0')
relocs = struct.pack('<QQqqQQqq', 4096, 0, 0, 0, 4104, 1, 0, 0)
valid = header + metadata + segments + name + relocs + bytes(8192)

def changed(data, offset, value):
    b = bytearray(data)
    struct.pack_into('<Q', b, offset, value)
    return b

cases = [
    ('valid mapping with unresolved import', valid, 0, 'PASS:'),
    ('truncated header', valid[:20], 2, 'truncated'),
    ('truncated image', valid[:-1], 2, 'truncated'),
    ('trailing bytes', valid+b'!', 2, 'trailing'),
    ('bad entry', changed(valid, 16, 8192), 2, 'invalid entry'),
    ('overlapping pages', changed(valid, 136, 0), 2, 'overlapping'),
    ('bad relocation target', changed(valid, 288, 8190), 2, 'invalid relocation'),
    ('bad import index', changed(valid, 336, 1), 2, 'invalid import'),
    ('oversized image', changed(valid, 8, 2**63), 2, 'invalid image'),
    ('unterminated name', valid[:160]+b'x'*128+valid[288:], 2, 'invalid import name'),
]
lines = []
with tempfile.TemporaryDirectory(prefix='second-son-loader-') as tmp:
    for title, data, expected, fragment in cases:
        target = Path(tmp)/'fixture.bin'
        target.write_bytes(data)
        run = subprocess.run([exe, target], capture_output=True, text=True, timeout=10)
        output = run.stdout + run.stderr
        assert run.returncode == expected and fragment in output, (title, run.returncode, output)
        lines.append('PASS: '+title)
(root/'outputs/loader-tests.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
