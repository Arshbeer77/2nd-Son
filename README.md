# Second Son Windows runtime probe

Source-only Windows compatibility-runtime research for inFAMOUS Second Son. Development resumed on 15 October 2026. No game files, decrypted executables, modules, extracted assets, prepared images, or toolchains are distributed.

GPL-2.0-or-later. Keep LICENSE and vendor attribution with source redistribution. This is a bounded compatibility-runtime diagnostic, not a playable port.

## Build and synthetic checks

Requires Windows x86-64, a 64-bit MinGW-w64 GCC toolchain with GNU objcopy, PowerShell, and Python 3. Build from workspace with:

```powershell
$env:SECOND_SON_GCC = "$PWD/work/toolchain/w64devkit/bin/gcc.exe"
& work/second-son-runtime/build.ps1
python work/second-son-runtime/tools/test_loader.py
python work/second-son-runtime/tools/test_entry.py
python work/second-son-runtime/tools/test_files.py
python work/second-son-runtime/tools/test_playgo.py
python work/second-son-runtime/tools/test_playgo_files.py
python work/second-son-runtime/tools/test_prepare.py
```

All 44 synthetic test cases pass: 10 loader, 3 entry, 8 file, 12 PlayGo lifecycle/metadata, 10 DMGP parser/readability cases, and 1 preparation regression. Outputs go to ignored `outputs/`; synthetic guest assembly and fixtures are created in temporary directories by the tests. No original game code is used by the synthetic tests.

## Runtime checkpoint and limits

Before entry, set:
- `SECOND_SON_PATCH_IMAGE0` and `SECOND_SON_BASE_IMAGE0` to the existing update/base Image0 directories.
- `SECOND_SON_PATCH_PLAYGO` and `SECOND_SON_BASE_PLAYGO` to the existing update/base Sc0/playgo-chunk.dat files.

The read-only `/app0` overlay tries update first, falling back to base only when the update file is missing. No game files are copied. Paths reject traversal and Windows alternate stream syntax; handles are checked against resolved roots. Only read-only open flags zero, read, seek, and close are implemented. Other flags, mounts, and unmapped host errors stop explicitly. Guest thread creation is still gated; descriptor access has only been exercised on the main thread.

Entry uses the confirmed System V argc/inline argv block and process-finalizer callback, an 8 MiB low-address stack between inaccessible pages, corresponding Windows TEB bounds, main-thread attributes, and existing FS TLS. Dynamic linked-module TLS is not yet fully implemented. An unexpected return stops with code 24, unsupported imports 20, exceptions 21, watchdog 22, initializer nonzero 23, and unsupported file operations 25. The finalizer logs and returns; real guest process-exit service is not yet reached/implemented. Five seconds covers initialization plus game execution after probe setup; mapping is outside that watchdog.

The TSC-frequency service reports hardware invariant TSC Hz using complete CPUID 0x15 data, or a three-sample bounded Windows-clock calibration with a one-percent stability check. This matches actual guest RDTSC instructions. It does not claim cycle-accurate PS4 timing.

## Standing session handoff policy

Before stopping, pausing, or approaching a context or usage limit, update this README with completed work, exact verification results, the current blocker, remaining uncertainty, and concrete next steps. Keep source changes and diagnostics in this canonical repository; never commit game inputs or personal filesystem paths. Unsupported guest services must stop explicitly, and metadata alone must never imply that all installation chunks are present.

## Verified Checkpoint — 15 October 2026

The runtime handles module `0x83` through a scoped PlayGo implementation, then `Initialize`, `Open`, `Close`, `SetLanguageMask`, and `Terminate`.
Initialize reads separate base and update Sc0 chunk metadata, validates their structure and matching identity/chunk domain (71 chunks each), and retains both. Open returns a real tracked handle (handle 1) for that metadata context. Other module IDs stop with code 20. Metadata problems or unsupported formats stop with code 26.

Actual bounded entry runs:
1. Linked initializers (libc at 0x7fb0000, fios at 0x80b0000) succeed with return code 0.
2. During initialization, Second Son registers its application heap and maps 1,207,959,552 bytes (1.125 GiB).
3. Game entry at 0x61dfd0 begins execution, queries invariant TSC (2688000000 Hz), opens `/app0/playgo.pgm` from patch (fd=3), reads 132,089 bytes and closes it.
4. Game calls `sceSysmoduleLoadModule(0x83)` (PlayGo), which loads the scoped service.
5. Game calls `scePlayGoInitialize`, validating 71 chunks in both base and patch Sc0 metadata.
6. Game calls `scePlayGoOpen`, opening handle 1.
7. `scePlayGoGetLocus` reports real file-backed status: chunk 0 pending (due to missing build marker), chunks 1..70 ready.
8. `sceSystemServiceParamGetInt` responds to param 1 (Lang) with 1 (English US).
9. Game sets language mask and builds initial ToDo list.
10. Execution advances to the next unmapped import:
   `STOP: unsupported import [206] gUPGiOQ1tmQ#a#b`
   `Name: scePlayGoSetToDoList`
   `Arguments: RDI=0x1 RSI=0x10009bf780 RDX=0x47 (71 entries)`
   `Guest return offset: 0x3c2b02`

Mapping reports 96 host-service and 368 unresolved relocation sites.
No title screen, gameplay, graphics, audio, input, or performance result.
