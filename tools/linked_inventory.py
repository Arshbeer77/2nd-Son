"""Read the linked image metadata without executing code."""
import argparse
import json
import struct
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('image', type=Path)
parser.add_argument('--prior', type=Path, help='Optional reference-comparison JSON')
parser.add_argument('--out', type=Path, default=Path(__file__).resolve().parents[1]/'out/linked-image-inventory.json')
args = parser.parse_args()
source = args.image
with source.open('rb') as f:
    def read(fmt):
        n = struct.calcsize(fmt)
        b = f.read(n)
        if len(b) != n:
            raise ValueError('Truncated metadata')
        return struct.unpack(fmt, b)
    magic, size, entry, ns, nr, ni, flags = read('<8s6Q')
    assert magic == b'BBPROBE5'
    procparam, = read('<Q')
    tls = read('<4Q')
    nm, = read('<Q')
    modules = [read('<7Q') for _ in range(nm)]
    nb, = read('<Q')
    bindings = [read('<3Q') for _ in range(nb)]
    segments = [read('<3Q') for _ in range(ns)]
    names = [read('<128s')[0].split(b'\0', 1)[0].decode('ascii') for _ in range(ni)]
    relocations = [read('<QQqq') for _ in range(nr)]
    image_offset = f.tell()
    assert source.stat().st_size == image_offset + size
bound = {b[0]: b for b in bindings}
prior = {x['encoded']: x for x in json.loads(args.prior.read_text())['imports']} if args.prior else {}
result = dict(image_size=size, image_offset=image_offset, entry=entry, flags=flags,
              procparam=procparam, tls=tls, modules=modules, segments=segments,
              relocation_count=nr, bindings=bindings,
              imports=[dict(encoded=n, index=i, native_binding=bound.get(i),
                            bloodborne=prior.get(n, {}).get('bloodborne', []),
                            gow3=prior.get(n, {}).get('gow3', [])) for i, n in enumerate(names)])
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(result, indent=2))
print(json.dumps({k: result[k] for k in ('image_size', 'image_offset', 'entry', 'relocation_count')}))
