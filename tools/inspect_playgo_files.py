"""Validate DMGP v3 file associations and report current extracted-file readability.

This diagnostic does not claim installed PlayGo locus, verify package hashes,
map Sc0 into the guest filesystem, or modify game inputs.
"""
import argparse
import json
import struct
from pathlib import Path, PurePosixPath


def span(data, off, size):
    if off < 0 or size < 0 or off > len(data) or size > len(data)-off:
        raise ValueError('DMGP reference outside file')
    return data[off:off+size]


def u32(data, off):
    return struct.unpack('<I', span(data, off, 4))[0]


def relative(data, field):
    """The observed reader sign-extends the low 32 bits of an eight-byte slot."""
    value, high = struct.unpack('<iI', span(data, field, 8))
    if high:
        raise ValueError('unsupported relative-pointer high word')
    return None if value == 0 else field+value


def string(data, off):
    if off is None:
        raise ValueError('required string pointer is null')
    span(data, off, 1)
    end = data.find(b'\0', off, min(len(data), off+4096))
    if end < 0:
        raise ValueError('unterminated or overlong DMGP string')
    return data[off:end].decode('utf-8', 'strict')


def count(data, field, maximum):
    value, high = struct.unpack('<iI', span(data, field, 8))
    if high or not 0 <= value <= maximum:
        raise ValueError('unsupported DMGP count')
    return value


def parse_pgm(data):
    if len(data) > 16*1024*1024 or span(data, 0, 8) != b'DMGP\x03\0\0\0':
        raise ValueError('expected bounded DMGP version 3')
    table = relative(data, 0x60)
    total = count(data, 0x68, 1000)
    if not total or table is None:
        raise ValueError('empty chunk table')
    span(data, table, total*56)
    chunks = []
    for i in range(total):
        record = table+i*56
        name = string(data, relative(data, record))
        file_table = relative(data, record+40)
        files = count(data, record+48, 100000)
        if files and file_table is None:
            raise ValueError('null nonempty file table')
        if file_table is not None:
            span(data, file_table, files*32)
        paths = []
        for j in range(files):
            f = file_table+j*32
            if span(data, f, 8) != bytes(8):
                raise ValueError('unsupported file-record prefix')
            if struct.unpack('<Q', span(data, f+24, 8))[0] != i:
                raise ValueError('file chunk ID mismatch')
            path = string(data, relative(data, f+16))
            parts = path.split('/')
            if not path or len(path) > 1023 or any(not p or p in ('.', '..') or p.endswith(('.', ' ')) for p in parts):
                raise ValueError('unsafe file-reference path')
            if any(ord(c)<32 or c in '\\:*?"<>|' for c in path):
                raise ValueError('unsafe file-reference character')
            paths.append(path)
        chunks.append(dict(id=i, label=name, files=paths))
    return chunks


def chunk_labels(data):
    if len(data)<256 or u32(data,0)!=0x6f676c70 or u32(data,16)!=len(data):
        raise ValueError('invalid Sc0 chunk metadata')
    total=struct.unpack('<H',span(data,10,2))[0]
    attrs,attrs_size,_,_,labels,labels_size=struct.unpack('<6I',span(data,192,24))
    if not 0<total<=1000 or attrs_size!=total*32:
        raise ValueError('invalid Sc0 chunk table')
    span(data,attrs,attrs_size); label_data=span(data,labels,labels_size)
    return [string(label_data,u32(data,attrs+i*32+28)) for i in range(total)]


def inspect(chunks, patch, base, patch_sc0=None, base_sc0=None):
    """Read one byte to check opening a regular file; never infer content validity."""
    roots=[('patch Image0',patch),('base Image0',base)]
    roots=[(name,Path(path).resolve(strict=True)) for name,path in roots]
    sc0=[(name,Path(path).resolve(strict=True)) for name,path in
         [('patch Sc0',patch_sc0),('base Sc0',base_sc0)] if path]
    results=[]
    for chunk in chunks:
        records=[]
        for path in chunk['files']:
            candidates=[(name,root,root.joinpath(*PurePosixPath(path).parts)) for name,root in roots]
            if path.startswith('sce_sys/'):
                candidates += [(name,root,root.joinpath(*PurePosixPath(path).parts[1:])) for name,root in sc0]
            status='missing'; source=None
            for name,root,candidate in candidates:
                try:
                    resolved=candidate.resolve(strict=True)
                    if not resolved.is_relative_to(root):
                        status='outside configured root'; break
                    if not resolved.is_file():
                        status='not a regular file'; break
                    with resolved.open('rb') as stream:
                        stream.read(1)
                    status='readable'; source=name; break
                except FileNotFoundError:
                    continue
                except OSError:
                    status='unreadable'; break
            records.append(dict(path=path,status=status,source=source))
        results.append(dict(id=chunk['id'],label=chunk['label'],references=records,
                            all_references_readable=all(x['status']=='readable' for x in records),
                            requires_sc0_mapping=any(x['source'] and 'Sc0' in x['source'] for x in records)))
    return dict(policy='diagnostic extracted-file readability, not installation status',chunks=results,
                limits=['No expected sizes or validated content hashes in these records.',
                        'Sc0 fallback is diagnostic only; the guest filesystem does not expose it.',
                        'Readability is a point-in-time observation, not a promise of future reads.'])


def read_limited(path, limit):
    with path.open('rb') as stream:
        data=stream.read(limit+1)
    if len(data)>limit:
        raise ValueError('metadata exceeds diagnostic size limit')
    return data


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pgm',type=Path); parser.add_argument('chunk_dat',type=Path)
    parser.add_argument('--patch',type=Path,required=True); parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--patch-sc0',type=Path); parser.add_argument('--base-sc0',type=Path)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    chunks=parse_pgm(read_limited(args.pgm,16*1024*1024))
    if [c['label'] for c in chunks]!=chunk_labels(read_limited(args.chunk_dat,1024*1024)):
        raise ValueError('PGM and Sc0 chunk labels or count disagree')
    report=inspect(chunks,args.patch,args.base,args.patch_sc0,args.base_sc0)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(report,indent=2)+'\n')
    readable=sum(c['all_references_readable'] for c in report['chunks'])
    print(f'{len(chunks)} chunks; {readable} have readable references under diagnostic mapping')


if __name__=='__main__':
    main()
