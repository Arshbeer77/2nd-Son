"""Small synthetic guest programs validate the one-way entry bridge and stop paths."""
import struct
import subprocess
import tempfile
from pathlib import Path
from test_support import root, gcc, objcopy
exe = root/'outputs/second-son-loader-check.exe'

checks = r"""
    cmpq $1,(%rdi)
    jne bad
    cmpq $0,16(%rdi)
    jne bad
    mov 8(%rdi),%rax
    cmpl $0x7070612f,(%rax)
    jne bad
    mov %rsp,%rax
    and $15,%eax
    cmp $8,%eax
    jne bad
    mov %fs:0,%rax
    cmp %rax,(%rax)
    jne bad
    test %rsi,%rsi
    jz bad
"""
cases = [
    ('startup ABI, stack, TLS, finalizer, unexpected return', checks+'sub $8,%rsp\ncall *%rsi\nadd $8,%rsp\nret',24,'unexpectedly returned'),
    ('unsupported guest import', 'call *trap(%rip)',20,'unsupported import'),
    ('bounded infinite guest', 'loop_here: jmp loop_here',22,'five-second limit'),
]
lines=[]
with tempfile.TemporaryDirectory(prefix='second-son-entry-') as tmp:
    tmp=Path(tmp)
    for title,code,expected,message in cases:
        source=tmp/'guest.S'; obj=tmp/'guest.o'; binary=tmp/'guest.bin'
        source.write_text('.text\n'+code+'\nbad: ud2\n.org 4096\n.quad 0\ntrap: .quad 0\n.org 8192\n')
        subprocess.run([gcc,'-c',source,'-o',obj],check=True)
        subprocess.run([objcopy,'-O','binary','-j','.text',obj,binary],check=True)
        header=struct.pack('<8s6Q',b'BBPROBE5',8192,0,2,1,1,0)
        metadata=struct.pack('<7Q',4096,0,0,0,0,0,0)
        segments=struct.pack('<6Q',0,4096,5,4096,4096,6)
        relocs=struct.pack('<QQqq',4104,1,0,0)
        fixture=tmp/'fixture.bin'
        fixture.write_bytes(header+metadata+segments+b'unknown#import'.ljust(128,b'\0')+relocs+binary.read_bytes())
        run=subprocess.run([exe,fixture,'--probe-entry'],capture_output=True,text=True,timeout=12)
        output=run.stdout+run.stderr
        assert run.returncode==expected and message in output,(title,run.returncode,output)
        if expected==24: assert 'finalizer callback reached' in output,output
        lines.append('PASS: '+title)
(root/'outputs/entry-tests.txt').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
