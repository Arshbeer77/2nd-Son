"""External toolchain configuration for synthetic tests (no game inputs)."""
import os
import shutil
from pathlib import Path
root = Path(__file__).resolve().parents[1]
gcc = os.environ.get('SECOND_SON_GCC') or shutil.which('gcc')
if not gcc:
    raise RuntimeError('Set SECOND_SON_GCC to your x86-64 MinGW GCC executable')
objcopy = os.environ.get('SECOND_SON_OBJCOPY') or str(Path(gcc).with_name('objcopy.exe'))
if not Path(objcopy).is_file():
    objcopy = shutil.which('objcopy')
if not objcopy:
    raise RuntimeError('Set SECOND_SON_OBJCOPY to GNU objcopy')
