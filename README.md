# 2nd-Son

Source-only Windows compatibility-runtime research for inFAMOUS Second Son. Development is paused at the checkpoint below. No game files, decrypted executables, modules, extracted assets, prepared images, or toolchains are distributed.

GPL-2.0-or-later. Keep LICENSE and vendor attribution with source redistribution. This is a bounded compatibility-runtime diagnostic, not a playable port.

## Build and synthetic checks

Requires Windows x86-64, a 64-bit MinGW-w64 GCC toolchain with GNU objcopy, PowerShell, and Python 3. The compiler and Python must be installed separately. From the repository directory:

```powershell
$env:SECOND_SON_GCC = 'D:/tools/w64devkit/bin/gcc.exe' # replace with your compiler
./build.ps1
python tools/test_loader.py
python tools/test_entry.py
python tools/test_files.py
```

Alternatively pass `./build.ps1 -Gcc <compiler-path>` or use a 64-bit MinGW GCC on PATH. Tests use `SECOND_SON_GCC` or PATH and the adjacent `objcopy.exe`; set `SECOND_SON_OBJCOPY` if it is elsewhere. The build rejects a non-x86-64 Windows target. `import_names.inc` is checked in; no private reports or external symbol database are needed to build. Outputs go to ignored `outputs/`; synthetic guest assembly and fixtures are created in temporary directories by the tests. No original game code is used by the synthetic tests.

## Local preparation utilities

Game inputs must be supplied separately and kept outside the repository. The preparation tools accept already plaintext SELF inputs; they do not decrypt encrypted packages. `game` is the directory containing the plaintext `eboot.bin` and its bundled `sce_module` directory. Use `--help` for options.

```powershell
python tools/prepare.py D:/private/game --out out
python tools/link_modules.py D:/private/game --out out
./outputs/second-son-loader-check.exe out/boot-linked.bin
```

`link_libc.py` preserves the earlier libc-only linker. `link_modules.py` is the multi-module successor. Mapping is the default; `--probe-initializers` executes linked initializers and `--probe-entry` executes a bounded entry probe. Prepared images contain user-supplied game code and must never be committed or uploaded. The ignore rules help exclude common input/output formats but are not a substitute for reviewing staged files.

Optional metadata/name tooling (not needed for build):

```powershell
python tools/linked_inventory.py out/boot-linked.bin --out out/linked-image-inventory.json
node tools/match_linked_nids.mjs D:/private/nids.csv out/linked-image-inventory.json out
python tools/generate_names.py out/linked-nid-resolution.json
```

The optional matcher requires Node.js and a separately supplied symbol CSV; it checks name hashes, not ABI correctness. No symbol CSV is included. Inventory `--prior` can add optional reference-comparison metadata.

## Runtime checkpoint and limits

Before entry, set SECOND_SON_PATCH_IMAGE0 and SECOND_SON_BASE_IMAGE0 to the existing update/base Image0 directories. The read-only /app0 overlay tries update first, falling back to base only when the update file is missing. No game files are copied. Paths reject traversal and Windows alternate stream syntax; handles are checked against resolved roots. Windows case-insensitive lookup currently applies (current inventory has no case collisions). Only read-only open flags zero, read, seek, and close are implemented. Other flags, mounts, and unmapped host errors stop explicitly. Guest thread creation is still gated; descriptor access has only been exercised on the main thread.

Entry uses the confirmed System V argc/inline argv block and process-finalizer callback, an 8 MiB low-address stack between inaccessible pages, corresponding Windows TEB bounds, main-thread attributes, and existing FS TLS. Dynamic linked-module TLS is not yet fully implemented. An unexpected return stops with code24, unsupported imports20, exceptions21, watchdog22, initializer nonzero23, and unsupported file operations25. The finalizer logs and returns; real guest process-exit service is not yet reached/implemented. Five seconds covers initialization plus game execution after probe setup; mapping is outside that watchdog.

The TSC-frequency service reports hardware invariant TSC Hz using complete CPUID15 data, or a three-sample bounded Windows-clock calibration with a one-percent stability check. This matches actual guest RDTSC instructions. It does not claim cycle-accurate PS4 timing.

Synthetic checks in `tools/test_loader.py`, `test_entry.py`, and `test_files.py` These exercise malformed images, actual synthetic x86-64 startup/stack/TLS/finalizer behavior, stop paths, read-only overlay precedence, real bytes/short reads/EOF, descriptor close, missing paths, path rejection, and sign-extended kernel errors.

Latest checkpoint: initializers succeed, entry reads all 132089 bytes of update /app0/playgo.pgm and closes it, then stops on sceSysmoduleLoadModule(0x83) at guest return offset 0x3c1f03. 87 host-service and 377 unresolved relocation sites, not unique-function counts. No title screen, gameplay, graphics, audio, input, or performance result.

Entry/TEB bridge technique adapted from GPL Bloodborne src_probe.c, commit e0a2a27ca898a85e6c2973520aa83293e87076d0 (Mrsuss60/bloodborne_pc_windows_port). Existing vendor code retains its attribution. Second Son entry ABI and native RDTSC use were independently confirmed by static binary inspection, not REA MCP. Kernel file signatures/error values checked against shadPS4 source; file wrappers here are scoped Windows implementations.

## Source and licensing

See [LICENSE](LICENSE) (GPL-2.0 text) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream provenance. This project is unaffiliated with Sony or Sucker Punch. Game names identify the research target; no ownership of their game content is claimed.

The repository contains the loader/resolver, entry and filesystem bridges, vendored host runtime, preparation/linking utilities, diagnostic import-name mapping, and synthetic tests. Research reports and whole unrelated reference repositories are intentionally outside this source package. Runtime behavior is unchanged by the standalone packaging work.
