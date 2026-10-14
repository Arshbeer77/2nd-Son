# Third-party notices and provenance

This source package preserves the GNU GPL version 2 license text in LICENSE. The local Second Son loader, resolver, probe and file bridge identify themselves as GPL-2.0-or-later; upstream code remains subject to its upstream license and notices. No proprietary game code or assets are licensed or distributed here.

## Bloodborne Windows port reference

Source: https://github.com/Mrsuss60/bloodborne_pc_windows_port

Inspected snapshot: `e0a2a27ca898a85e6c2973520aa83293e87076d0`.

Credit belongs to Mrsuss60 and the upstream contributors. The reference snapshot supplies the GPL version 2 license text; that text is preserved verbatim as LICENSE. No new ownership claim is made over these files.

The following files are byte-identical to the corresponding upstream `src/` files in the inspected local reference snapshot:

- `vendor/host_sync.h`
- `vendor/runtime.h`
- `vendor/runtime_host.c`
- `vendor/runtime_memory.c`
- `vendor/runtime_mutex.c`
- `vendor/win32_compat.c`
- `vendor/win32_compat.h`
- `vendor/win32_memory.c`
- `vendor/win32_memory.h`

`vendor/runtime_thread.c` derives from upstream `src/runtime_thread.c`, with a local `runtime_thread_set_probe_stack` helper to describe the bounded guest stack. `tools/prepare.py` and `tools/link_modules.py` derive from upstream `scripts/prepare.py` and `scripts/link_modules.py` from the inspected snapshot; prepare.py has only a provenance-docstring adjustment. `tools/link_libc.py` preserves the local predecessor to the multi-module workflow.

The entry/TEB bridge technique in `probe.h` is adapted from upstream `src/probe.c` (saved locally as `src_probe.c`), with Second Son-specific entry ABI, stack and bounded stop handling. Existing identifiers and comments are retained. The packaging changes make build/test paths independent of the original workspace; they do not alter runtime execution behavior.

## shadPS4

Source: https://github.com/shadps4-emu/shadPS4

Inspected snapshot: `7295d120b61c499cb309521ff1f5b898c5d62051`.

`probe.h` retains the attribution "Copyright 2024 shadPS4 Emulator Project" and GPL-2.0-or-later notice for the `HeapTraceInfo` layout and heap-trace state contract adapted from `libc_internal_memory.cpp`. Kernel file signatures and error values were checked against shadPS4; the scoped Windows file wrappers are local implementations. Whole shadPS4 source files are not bundled.

## Diagnostic names and formats

`import_names.inc` contains API-name/NID diagnostic mappings, not executable game bytes. The optional CSV matcher verifies symbol-name hashes but does not establish runtime compatibility or ABI correctness. A user's external symbol database is not included.

The plaintext SELF/ELF preparation implementation and its BBPROBE image format are inherited from the Bloodborne reference above. GNU toolchains, Python and Node.js are external prerequisites and are not redistributed. Synthetic tests create their own minimal machine code from the assembly text visible in the test sources.
