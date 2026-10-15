# 2nd-Son

Source-only Windows compatibility-runtime research for inFAMOUS Second Son. Development resumed on 15 October 2026 from the checkpoint below. No game files, decrypted executables, modules, extracted assets, prepared images, or toolchains are distributed.

GPL-2.0-or-later. Keep LICENSE and vendor attribution with source redistribution. This is a bounded compatibility-runtime diagnostic, not a playable port.

## Build and synthetic checks

Requires Windows x86-64, a 64-bit MinGW-w64 GCC toolchain with GNU objcopy, PowerShell, and Python 3. The compiler and Python must be installed separately. From the repository directory:

```powershell
$env:SECOND_SON_GCC = 'D:/tools/w64devkit/bin/gcc.exe' # replace with your compiler
./build.ps1
python tools/test_loader.py
python tools/test_entry.py
python tools/test_files.py
python tools/test_playgo.py
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

Prior checkpoint (superseded by the PlayGo checkpoint below): initializers succeed, entry reads all 132089 bytes of update /app0/playgo.pgm and closes it, then stops on sceSysmoduleLoadModule(0x83) at guest return offset 0x3c1f03. 87 host-service and 377 unresolved relocation sites, not unique-function counts. No title screen, gameplay, graphics, audio, input, or performance result.

Entry/TEB bridge technique adapted from GPL Bloodborne src_probe.c, commit e0a2a27ca898a85e6c2973520aa83293e87076d0 (Mrsuss60/bloodborne_pc_windows_port). Existing vendor code retains its attribution. Second Son entry ABI and native RDTSC use were independently confirmed by static binary inspection, not REA MCP. Kernel file signatures/error values checked against shadPS4 source; file wrappers here are scoped Windows implementations.

## Source and licensing

See [LICENSE](LICENSE) (GPL-2.0 text) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for upstream provenance. This project is unaffiliated with Sony or Sucker Punch. Game names identify the research target; no ownership of their game content is claimed.

The repository contains the loader/resolver, entry and filesystem bridges, vendored host runtime, preparation/linking utilities, diagnostic import-name mapping, and synthetic tests. Research reports and whole unrelated reference repositories are intentionally outside this source package. The original standalone packaging preserved runtime behavior; subsequent runtime changes are recorded below.

## Standing session handoff policy

Before stopping, pausing, or approaching a context or usage limit, update this README with completed work, exact verification results, the current blocker, remaining uncertainty, and concrete next steps. Keep source changes and diagnostics in this canonical repository; never commit game inputs or personal filesystem paths. Unsupported guest services must stop explicitly, and metadata alone must never imply that all installation chunks are present.


## PlayGo checkpoint — 15 October 2026

Development is resumed. The portable x86-64 GCC build passes with warnings treated as errors. All 33 synthetic cases pass: 10 loader, 3 entry, 8 file, and 12 PlayGo cases. PlayGo tests compile a synthetic C caller (no game bytes) and check lifecycle/errors, malformed lengths/magic/section bounds/overlaps, record counts, invalid microchunk/chunk/label/scenario references, base/update identity mismatch, and unsupported module stop. The original 21 cases also passed before this change.

The runtime now handles module 0x83 through a scoped PlayGo implementation, then Initialize, Open, Close, SetLanguageMask and Terminate. Initialize reads separate base and update Sc0 chunk metadata, validates their structure and matching identity/chunk domain, and retains both. Open returns a real tracked handle for that metadata context. This is not a general sysmodule loader: other module IDs stop with code 20. Metadata problems or unsupported formats stop with code 26. Availability, ETA, progress and download scheduling remain unresolved imports rather than fabricated successful results.

In addition to Image0 configuration, set SECOND_SON_BASE_PLAYGO and SECOND_SON_PATCH_PLAYGO to the existing base/update Sc0/playgo-chunk.dat files. These are explicit host metadata paths, not virtual game paths. No files are copied into Image0. The supported metadata profile is intentionally narrow: one image; nonempty chunk/microchunk/scenario tables up to 1000/8000/32 entries; 256-byte header; version words (0,0) or (0x3000,0); matching 128-byte content-ID fields and equal chunk counts across base/update. The latter are target-specific checks, not a claim that every valid PlayGo package follows them. Bounds checks validate the records used for lifecycle; package ranges and installation completeness are not verified.

Actual bounded entry now reads update playgo.pgm, loads the scoped service, validates 71 chunks in each metadata file, initializes and opens handle 1. It then stops with code 20 at scePlayGoGetLocus, import 200, handle 1, count 71, guest return offset 0x3c3600. Mapping reports 94 host-service and 370 unresolved relocation sites. Both linked initializers return zero. Diagnostic output is in ignored outputs/entry-playgo.txt. No gameplay or rendering has been demonstrated.

During this change, the first successful Open was followed by a null access at the game's FS:0 instruction (offset 0x5b9dfd). Restoring guest FS after the new host-side lifecycle operations resolves that fault and reaches the explicit GetLocus dependency. This reinforces the need to preserve guest TLS around future Windows service boundaries; it does not establish complete dynamic-module TLS support.

Next steps: establish a verified mapping from the game's DMGP playgo.pgm chunk/file records to the read-only update/base asset view, including every file dependency and a declared verification policy, before implementing GetLocus. Expected file sizes are not established by the currently observed DMGP records. Sc0 requested locus 3 is not evidence of installed locus 3, and file counts or aggregate sizes do not prove extraction completeness. If the mapping remains unverified, keep the explicit stop. Continue bounded entry probes only after evidence-backed changes, and update this checkpoint before any stop or context/usage limit.


### DMGP diagnostic and entry-slice correction

The new tools/inspect_playgo_files.py parses the observed DMGP v3 chunk/file tables with signed 32-bit field-relative pointers, null-pointer handling, count and range limits, chunk-ID checks, and safe relative paths. It cross-checks all labels against Sc0 metadata. Optional Sc0 roots are explicitly a diagnostic fallback; this utility does not add guest filesystem mappings or generate trusted installation state. It opens each resolved regular file read-only and reads up to one byte, reporting missing, unreadable, escaping-root and readable results separately. It does not infer expected length or interpret the opaque record word as a content hash.

Example (all inputs remain external):

```powershell
python tools/inspect_playgo_files.py D:/private/update/Image0/playgo.pgm D:/private/update/Sc0/playgo-chunk.dat --patch D:/private/update/Image0 --base D:/private/base/Image0 --patch-sc0 D:/private/update/Sc0 --base-sc0 D:/private/base/Sc0 --out outputs/playgo-readability.json
python tools/test_playgo_files.py
python tools/test_prepare.py
```

Actual current update diagnostic: 71 labels match; 70 chunks have readable references under this diagnostic mapping. Bootstrap chunk 0 references the missing file `++ retail 1118.1617 i3p7_eu ship ++`. It is an ordinary file record; no evidence establishes that it is optional. The diagnostic Sc0 fallback also exceeds current guest mapping. Consequently GetLocus remains explicitly unresolved: this change does not turn host-side diagnostic readability into a guest installed-state claim. A future explicitly documented extracted-files policy must align guest lookup behavior with its checks and preserve bootstrap incompleteness. Expected sizes and content hashes remain unavailable in the observed DMGP records; they are limitations, not fields the parser can invent.

Preparation now writes entry.bin from the ELF virtual entry address in the mapped image instead of offset zero. Existing prepared artifacts are not rewritten. A synthetic nonzero-entry/short-tail/bounds regression passes. Ten new DMGP parser/readability cases pass, including backward pointers, malformed records, traversal, overlay precedence, missing references and explicitly requested Sc0 fallback. Together with the previous 33 cases and one preparation regression, 44 cases have passed. The actual runtime checkpoint remains GetLocus at 0x3c3600; diagnostic-only changes do not alter that execution path.
