"""Regression: ELF entry addresses differ from the image start."""
from prepare import entry_excerpt
image = b'not entry'.ljust(4096, b'\0') + b'\x55\x48\x89\xe5\xc3'
assert entry_excerpt(image, 4096) == b'\x55\x48\x89\xe5\xc3'
assert entry_excerpt(image, 4096, 2) == b'\x55\x48'
for entry in (-1, len(image), len(image)+1):
    try:
        entry_excerpt(image, entry)
    except ValueError:
        pass
    else:
        raise AssertionError('out-of-image entry accepted')
print('PASS: nonzero ELF entry excerpt, short tail, bounds')
