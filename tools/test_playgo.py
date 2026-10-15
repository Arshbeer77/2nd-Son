"""Synthetic PlayGo lifecycle and bounded metadata validation; no game data."""
import os
import struct
import subprocess
import tempfile
from pathlib import Path
from test_support import root, gcc


def metadata():
    b = bytearray(368)
    struct.pack_into('<I6HIH', b, 0, 0x6f676c70, 0, 0, 1, 1, 1, 1, len(b), 0)
    b[64:69] = b'TEST\0'
    sections = [(256, 32), (288, 2), (290, 2), (304, 16),
                (320, 32), (352, 2), (354, 2), (368, 0)]
    for i, pair in enumerate(sections):
        struct.pack_into('<II', b, 192+i*8, *pair)
    b[258] = 3
    struct.pack_into('<H', b, 270, 1)
    struct.pack_into('<Q', b, 312, 10)
    struct.pack_into('<HH', b, 340, 1, 1)
    return b

with tempfile.TemporaryDirectory(prefix='second-son-playgo-') as folder:
    folder = Path(folder)
    source = folder/'test.c'
    source.write_text(r'''
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void restore_guest_fs(void) {}
#include "playgo.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
int main(int argc,char **argv) {
    (void)argc;
    if (!strcmp(argv[1],"module")) { guest_sysmodule_load(0x84); return 1; }
    unsigned char buffer[1]; PlayGoInit p={buffer,0x200000,0}; uint32_t h=99;
    CHECK(guest_playgo_initialize(NULL)==PG_ERROR(10));
    CHECK(guest_playgo_open(&h,NULL)==PG_ERROR(5) && h==99);
    CHECK(guest_playgo_terminate()==PG_ERROR(5));
    p.size=1; CHECK(guest_playgo_initialize(&p)==PG_ERROR(11)); p.size=0x200000;
    p.reserved=1; CHECK(guest_playgo_initialize(&p)==PG_ERROR(4)); p.reserved=0;
    CHECK(guest_sysmodule_load(0x83)==0);
    CHECK(guest_playgo_initialize(&p)==0);
    CHECK(guest_playgo_initialize(&p)==PG_ERROR(6));
    CHECK(guest_playgo_open(NULL,NULL)==PG_ERROR(10));
    CHECK(guest_playgo_open(&h,buffer)==PG_ERROR(4) && h==99);
    CHECK(guest_playgo_open(&h,NULL)==0 && h==1);
    uint16_t id0=0, id1=1; int8_t locus=-1;
    CHECK(guest_playgo_get_locus(2,&id0,1,&locus)==PG_ERROR(9));
    CHECK(guest_playgo_get_locus(1,NULL,1,&locus)==PG_ERROR(10));
    CHECK(guest_playgo_get_locus(1,&id0,0,&locus)==PG_ERROR(4));
    CHECK(guest_playgo_get_locus(1,&id1,1,&locus)==PG_ERROR(12));
    CHECK(guest_playgo_get_locus(1,&id0,1,&locus)==0 && locus==0);
    CHECK(guest_playgo_language(2,7)==PG_ERROR(9));
    CHECK(guest_playgo_language(1,7)==0 && playgo_language_mask==7);
    CHECK(guest_playgo_close(1)==0);
    CHECK(guest_playgo_language(1,8)==PG_ERROR(9));
    CHECK(guest_playgo_close(1)==PG_ERROR(9));
    CHECK(guest_playgo_open(&h,NULL)==0);
    CHECK(guest_playgo_terminate()==0 && !playgo_metadata[0].bytes && !playgo_metadata[1].bytes);
    CHECK(guest_playgo_initialize(&p)==0);
    CHECK(guest_playgo_terminate()==0);
    return 0;
}
''')
    exe=folder/'test.exe'
    subprocess.run([gcc,'-std=c11','-Wall','-Wextra','-Werror','-O2','-I',root,source,'-o',exe],check=True)
    base=folder/'base.dat'; patch=folder/'patch.dat'
    env=os.environ.copy()
    env.update(SECOND_SON_BASE_PLAYGO=str(base),SECOND_SON_PATCH_PLAYGO=str(patch))
    cases=[('lifecycle and API errors',lambda b:None,0),
           ('bad magic',lambda b:struct.pack_into('<I',b,0,0),26),
           ('short header',lambda b:b.clear(),26),
           ('section outside file',lambda b:struct.pack_into('<I',b,192,9999),26),
           ('overlapping sections',lambda b:struct.pack_into('<I',b,200,256),26),
           ('count mismatch',lambda b:struct.pack_into('<H',b,10,2),26),
           ('invalid microchunk',lambda b:struct.pack_into('<H',b,288,1),26),
           ('invalid chunk label',lambda b:struct.pack_into('<I',b,284,2),26),
           ('invalid scenario ID',lambda b:struct.pack_into('<H',b,352,1),26),
           ('invalid initial count',lambda b:struct.pack_into('<H',b,340,2),26),
           ('base patch identity mismatch',lambda b:b.__setitem__(64,88),26)]
    lines=[]
    for title,mutate,code in cases:
        base.write_bytes(metadata()); data=metadata(); mutate(data); patch.write_bytes(data)
        result=subprocess.run([exe,'test'],capture_output=True,text=True,env=env,timeout=5)
        assert result.returncode==code,(title,result.returncode,result.stdout,result.stderr)
        lines.append('PASS: '+title)
    result=subprocess.run([exe,'module'],capture_output=True,text=True,env=env,timeout=5)
    assert result.returncode==20 and '0x84' in result.stderr
    lines.append('PASS: unsupported module stops explicitly')
    (root/'outputs/playgo-tests.txt').write_text('\n'.join(lines)+'\n')
    print('\n'.join(lines))
