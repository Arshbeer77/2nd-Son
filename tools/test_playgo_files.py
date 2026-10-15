"""Synthetic DMGP bounds, signed pointers, association and readability tests."""
import struct
import tempfile
from pathlib import Path
from inspect_playgo_files import parse_pgm, inspect


def fixture():
    b=bytearray(320); b[:8]=b'DMGP\x03\0\0\0'
    struct.pack_into('<i',b,0x60,208-0x60); struct.pack_into('<i',b,0x68,1)
    b[128:133]=b'boot\0'; b[144:154]=b'file.bin\0\0'
    struct.pack_into('<i',b,208,128-208)
    struct.pack_into('<i',b,248,264-248); struct.pack_into('<i',b,256,1)
    struct.pack_into('<i',b,280,144-280)
    return b

chunks=parse_pgm(fixture())
assert chunks==[dict(id=0,label='boot',files=['file.bin'])]
print('PASS: signed backward string pointers and associations')
mutations=[('pointer underflow',lambda b:struct.pack_into('<i',b,208,-1000)),
 ('null required label',lambda b:struct.pack_into('<i',b,208,0)),
 ('null file list',lambda b:struct.pack_into('<i',b,248,0)),
 ('negative count',lambda b:struct.pack_into('<i',b,256,-1)),
 ('truncated file list',lambda b:struct.pack_into('<i',b,256,100)),
 ('wrong chunk ID',lambda b:struct.pack_into('<Q',b,288,1)),
 ('path traversal',lambda b:b.__setitem__(slice(144,154),b'../bad\0\0\0\0')),
 ('pointer high word',lambda b:struct.pack_into('<I',b,212,1))]
for name,mutate in mutations:
    data=fixture(); mutate(data)
    try: parse_pgm(data)
    except ValueError: pass
    else: raise AssertionError(name)
    print('PASS: '+name)
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp); base=tmp/'base';patch=tmp/'patch';sc0=tmp/'sc0'
    for p in (base,patch,sc0):p.mkdir()
    (base/'file.bin').write_bytes(b'base');(patch/'file.bin').write_bytes(b'patch')
    report=inspect(chunks,patch,base)
    assert report['chunks'][0]['references'][0]['source']=='patch Image0'
    (patch/'file.bin').unlink()
    assert inspect(chunks,patch,base)['chunks'][0]['references'][0]['source']=='base Image0'
    (base/'file.bin').unlink()
    assert not inspect(chunks,patch,base)['chunks'][0]['all_references_readable']
    (sc0/'param.sfo').write_bytes(b'metadata')
    c=[dict(id=0,label='boot',files=['sce_sys/param.sfo'])]
    assert not inspect(c,patch,base)['chunks'][0]['all_references_readable']
    r=inspect(c,patch,base,sc0)['chunks'][0]
    assert r['all_references_readable'] and r['requires_sc0_mapping']
    print('PASS: patch/base precedence, missing dependency, explicit Sc0 diagnostic mapping')
