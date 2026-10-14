/* SPDX-License-Identifier: GPL-2.0-or-later
 * Windows mapping diagnostic for the BBPROBE5 image format.
 * Guest execution is opt-in and bounded. Unsupported imports remain inaccessible.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t address, size, flags; } Segment;
typedef struct { uint64_t base, size, init, tls, memsz, filesz, id; } Module;
typedef struct { uint64_t index, address, kind; } Binding;
typedef struct { uint64_t target, kind; int64_t value, addend; } Reloc;
static void fail(const char *s) { fprintf(stderr, "ERROR: %s\n", s); exit(2); }
static void read_exact(FILE *f, void *p, size_t n) {
    if (fread(p, 1, n, f) != n) fail("truncated image");
}
static uint64_t word(FILE *f) { uint64_t v; read_exact(f, &v, 8); return v; }
static int within(uint64_t a, uint64_t n, uint64_t size) {
    return a <= size && n <= size - a;
}
static uint64_t page(uint64_t n) { return (n + 4095) & ~UINT64_C(4095); }
static int mapped(Segment *s, uint64_t ns, uint64_t a, uint64_t n, int executable) {
    for (uint64_t i = 0; i < ns; ++i)
        if (a >= s[i].address && within(a - s[i].address, n, s[i].size)
            && (!executable || (s[i].flags & 1))) return 1;
    return 0;
}
static DWORD protection(uint64_t flags) {
    if (flags & 1) return (flags & 2) ? PAGE_EXECUTE_READWRITE : (flags & 4) ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    return (flags & 2) ? PAGE_READWRITE : (flags & 4) ? PAGE_READONLY : PAGE_NOACCESS;
}
#include "probe.h"
int main(int argc, char **argv) {
    int entry_probe = argc == 3 && !strcmp(argv[2], "--probe-entry");
    int probe = entry_probe || (argc == 3 && !strcmp(argv[2], "--probe-initializers"));
    if (argc != 2 && !probe) { fprintf(stderr, "Usage: second-son-loader-check.exe boot-linked.bin [--probe-initializers|--probe-entry]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) fail("cannot open linked image");
    char magic[8]; read_exact(f, magic, 8);
    if (memcmp(magic, "BBPROBE5", 8)) fail("unsupported image format");
    uint64_t size = word(f), entry = word(f), ns = word(f), nr = word(f), ni = word(f), flags = word(f);
    if (!size || size > UINT64_C(1073741824) || !ns || ns > 256 || nr > 10000000 || !ni || ni > 65536 || flags > 1)
        fail("invalid image header or unsupported limits");
    uint64_t procparam = word(f), tls[4]; read_exact(f, tls, sizeof(tls));
    if (tls[1] > tls[2] || tls[2] > 1048576 || !within(tls[0], tls[1], size)
        || (tls[3] && ((tls[3] & (tls[3]-1)) || tls[3] > 4096))) fail("invalid main TLS");
    uint64_t nm = word(f); if (nm > 32) fail("too many modules");
    Module *mods = calloc(nm ? nm : 1, sizeof(*mods));
    if (!mods) fail("allocation failed");
    read_exact(f, mods, nm * sizeof(*mods));
    uint64_t nb = word(f); if (nb > ni) fail("too many bindings");
    Binding *bindings = calloc(nb ? nb : 1, sizeof(*bindings));
    Segment *segments = calloc(ns, sizeof(*segments));
    char (*names)[128] = calloc(ni, 128);
    Reloc *relocs = calloc(nr ? nr : 1, sizeof(*relocs));
    uint64_t *targets = calloc(ni, sizeof(*targets)), *kinds = calloc(ni, sizeof(*kinds));
    if (!bindings || !segments || !names || !relocs || !targets || !kinds) fail("allocation failed");
    read_exact(f, bindings, nb * sizeof(*bindings));
    read_exact(f, segments, ns * sizeof(*segments));
    read_exact(f, names, ni * 128);
    read_exact(f, relocs, nr * sizeof(*relocs));
    for (uint64_t i = 0; i < ns; ++i) {
        Segment s = segments[i];
        if (!s.size || s.address % 4096 || s.flags > 7 || !within(s.address, s.size, size)) fail("invalid segment");
        for (uint64_t j = 0; j < i; ++j)
            if (s.address < segments[j].address + page(segments[j].size)
                && segments[j].address < s.address + page(s.size)) fail("overlapping segment pages");
    }
    if (!mapped(segments, ns, entry, 1, 1) || !mapped(segments, ns, procparam, 8, 0)) fail("invalid entry or process parameters");
    if (tls[1] && !mapped(segments, ns, tls[0], tls[1], 0)) fail("unmapped main TLS template");
    for (uint64_t i = 0; i < nm; ++i) {
        Module m = mods[i];
        if (!within(m.base, m.size, size) || !within(m.init-m.base, 1, m.size)
            || !mapped(segments, ns, m.init, 1, 1) || m.filesz > m.memsz || m.memsz > 1048576
            || (m.id && (m.id < 2 || m.id > 32))
            || (m.filesz && !mapped(segments, ns, m.tls, m.filesz, 0))) fail("invalid module metadata");
        for (uint64_t j=0; j<i; ++j) if (m.id && m.id == mods[j].id) fail("duplicate TLS module id");
    }
    for (uint64_t i = 0; i < ni; ++i) if (!names[i][0] || !memchr(names[i], 0, 128)) fail("invalid import name");
    for (uint64_t i = 0; i < nb; ++i) {
        Binding b = bindings[i];
        if (b.index >= ni || kinds[b.index] || (b.kind != 1 && b.kind != 2)
            || !mapped(segments, ns, b.address, 1, b.kind == 1)) fail("invalid native binding");
        int in_module = 0;
        for (uint64_t j=0; j<nm; ++j) if (b.address >= mods[j].base && within(b.address-mods[j].base, 1, mods[j].size)) in_module=1;
        if (!in_module) fail("binding outside linked modules");
        targets[b.index] = b.address; kinds[b.index] = b.kind;
    }
    for (uint64_t i = 0; i < nr; ++i) {
        Reloc r = relocs[i];
        if (!mapped(segments, ns, r.target, 8, 0) || r.kind > 2 || r.value < 0 || r.addend < 0) fail("invalid relocation");
        if (!r.kind) {
            if ((uint64_t)r.value > size || r.addend) fail("invalid relative relocation");
        } else {
            if ((uint64_t)r.value >= ni || r.addend >= 4096 || (r.kind == 1 && r.addend)) fail("invalid import relocation");
            if (kinds[r.value] && (kinds[r.value] != r.kind || !mapped(segments, ns, targets[r.value], r.addend+1, r.kind==1))) fail("binding/relocation mismatch");
        }
    }
    unsigned char *image = probe ? runtime_low_map(page(size),3) : VirtualAlloc(NULL, page(size), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    unsigned char *traps = VirtualAlloc(NULL, ni * 4096, MEM_RESERVE, PAGE_NOACCESS);
    if (!image || !traps) fail("Windows allocation failed");
    read_exact(f, image, size);
    if (fgetc(f) != EOF) fail("unexpected trailing bytes");
    fclose(f);
    guest_procparam_address=image+procparam;
    uint64_t native_relocs=0, unresolved_relocs=0, relative_relocs=0, host_relocs=0;
    for (uint64_t i = 0; i < nr; ++i) {
        Reloc r = relocs[i]; uintptr_t v;
        if (!r.kind) { v = (uintptr_t)image + r.value; ++relative_relocs; }
        else if (probe && (v=probe_resolve(names[r.value],r.kind))) { v+=r.addend; ++host_relocs; }
        else if (kinds[r.value]) { v = (uintptr_t)image + targets[r.value] + r.addend; ++native_relocs; }
        else { v = (uintptr_t)traps + r.value * 4096 + r.addend; ++unresolved_relocs; }
        memcpy(image + r.target, &v, 8);
        uintptr_t check; memcpy(&check, image+r.target, 8);
        if (check != v) fail("relocation readback failed");
    }
    DWORD old;
    if (!VirtualProtect(image, page(size), PAGE_NOACCESS, &old)) fail("image protection failed");
    for (uint64_t i=0; i<ns; ++i) {
        Segment s=segments[i]; DWORD p=protection(s.flags);
        if (!VirtualProtect(image+s.address, page(s.size), p, &old)) fail("segment protection failed");
        for (uint64_t off=0; off<page(s.size); off+=4096) {
            MEMORY_BASIC_INFORMATION info;
            if (!VirtualQuery(image+s.address+off, &info, sizeof(info)) || info.Protect!=p) fail("segment protection readback failed");
        }
    }
    printf("PASS: 64-bit Windows image mapping and relocation check\n");
    printf("Image: %" PRIu64 " bytes; segments: %" PRIu64 "; linked modules: %" PRIu64 "\n",size,ns,nm);
    printf("Imports: %" PRIu64 "; native bindings: %" PRIu64 "; require host runtime: %" PRIu64 "\n",ni,nb,ni-nb);
    printf("Relocations: %" PRIu64 " relative, %" PRIu64 " native, %" PRIu64 " unresolved\n",relative_relocs,native_relocs,unresolved_relocs);
    if (probe) printf("Host service relocations: %" PRIu64 "\n",host_relocs);
    if (probe) trace_initializers(image,size,traps,ni,names,mods,nm,tls,entry_probe,entry);
    printf("Guest code was NOT executed. This diagnostic is not a playable port.\n");
    VirtualFree(image,0,MEM_RELEASE); VirtualFree(traps,0,MEM_RELEASE);
    free(mods); free(bindings); free(segments); free(names); free(relocs); free(targets); free(kinds);
    return 0;
}
