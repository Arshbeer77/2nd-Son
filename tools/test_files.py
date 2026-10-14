"""Exercise read-only overlay and kernel seek/error conventions with synthetic guests."""
import os
import struct
import subprocess
import tempfile
from pathlib import Path
from test_support import root, gcc, objcopy
exe=root/'outputs/second-son-loader-check.exe'
lines=[]
with tempfile.TemporaryDirectory(prefix='second-son-files-') as tmp:
    tmp=Path(tmp); base=tmp/'base'; patch=tmp/'patch'; base.mkdir(); patch.mkdir()
    (base/'overlay').write_bytes(b'base'); (patch/'overlay').write_bytes(b'updated!!')
    (base/'base-only').write_bytes(b'base-only-file')
    env=os.environ.copy(); env['SECOND_SON_BASE_IMAGE0']=str(base); env['SECOND_SON_PATCH_IMAGE0']=str(patch)
    def seek(offset,whence,result):
        return f'mov %eax,%edi\nmov ${offset},%rsi\nmov ${whence},%edx\ncall *seek_fn(%rip)\nmovabs ${result},%rcx\ncmp %rcx,%rax\njne bad\n'
    read_check='mov %eax,%ebx\nmov %ebx,%edi\nlea buffer(%rip),%rsi\nmov $32,%edx\ncall *read_fn(%rip)\ncmp $9,%rax\njne bad\ncmpl $0x61647075,buffer(%rip)\njne bad\nmov %ebx,%edi\nlea buffer(%rip),%rsi\nmov $32,%edx\ncall *read_fn(%rip)\ntest %rax,%rax\njnz bad\nmov %ebx,%edi\ncall *close_fn(%rip)\ntest %eax,%eax\njnz bad\nmov %ebx,%edi\nlea buffer(%rip),%rsi\nmov $1,%edx\ncall *read_fn(%rip)\nmovabs $0xffffffff80020009,%rcx\ncmp %rcx,%rax\njne bad\n'
    cases=[
        ('read bytes, short read, EOF, close, sign-extended EBADF','/app0/overlay',0,3,read_check,24),
        ('patch overrides base','/app0/overlay',0,3,seek(0,2,9),24),
        ('base fallback','/app0/base-only',0,3,seek(0,2,14),24),
        ('missing returns ENOENT','/app0/missing',0,0x80020002,'',24),
        ('traversal rejected','/app0/../outside',0,0x8002000d,'',24),
        ('Windows stream syntax rejected','/app0/overlay:stream',0,0x8002000d,'',24),
        ('write flags stop explicitly','/app0/overlay',1,0,'',25),
        ('negative seek sign extends EINVAL','/app0/overlay',0,3,seek(-1,0,-2147352554),24),
    ]
    for title,path,flags,fd,after,expected in cases:
        source=tmp/'guest.S'; obj=tmp/'guest.o'; binary=tmp/'guest.bin'
        source.write_text(f'.text\nsub $8,%rsp\nlea path(%rip),%rdi\nmov ${flags},%esi\nxor %edx,%edx\ncall *open_fn(%rip)\ncmp ${fd},%eax\njne bad\n'+after+'add $8,%rsp\nret\nbad: ud2\npath: .asciz "'+path+'"\n.org 4096\n.quad 0\nopen_fn: .quad 0\nseek_fn: .quad 0\nread_fn: .quad 0\nclose_fn: .quad 0\nbuffer: .space 32\n.org 8192\n')
        subprocess.run([gcc,'-c',source,'-o',obj],check=True)
        subprocess.run([objcopy,'-O','binary','-j','.text',obj,binary],check=True)
        header=struct.pack('<8s6Q',b'BBPROBE5',8192,0,2,4,4,0)
        metadata=struct.pack('<7Q',4096,0,0,0,0,0,0)
        segments=struct.pack('<6Q',0,4096,5,4096,4096,6)
        names=b'1G3lF1Gg1k8#b#T'.ljust(128,b'\0')+b'oib76F-12fk#b#T'.ljust(128,b'\0')+b'Cg4srZ6TKbU#b#T'.ljust(128,b'\0')+b'UK2Tl2DWUns#b#T'.ljust(128,b'\0')
        relocs=b''.join(struct.pack('<QQqq',4104+i*8,1,i,0) for i in range(4))
        fixture=tmp/'fixture.bin'; fixture.write_bytes(header+metadata+segments+names+relocs+binary.read_bytes())
        run=subprocess.run([exe,fixture,'--probe-entry'],capture_output=True,text=True,timeout=12,env=env)
        assert run.returncode==expected,(title,run.returncode,run.stdout,run.stderr)
        lines.append('PASS: '+title)
(root/'outputs/file-tests.txt').write_text('\n'.join(lines)+'\n'); print('\n'.join(lines))
