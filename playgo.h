/* SPDX-License-Identifier: GPL-2.0-or-later
 * Lifecycle ABI and metadata layout informed by shadPS4 (Copyright 2024-2026
 * shadPS4 Emulator Project). Metadata never establishes installed availability. */
typedef struct { void *buffer; uint32_t size, reserved; } PlayGoInit;
typedef struct { unsigned char *bytes; uint32_t size; uint16_t chunks; } PlayGoMetadata;
static PlayGoMetadata playgo_metadata[2]; /* base and patch stay separate */
static int playgo_loaded, playgo_initialized, playgo_opened;
static uint64_t playgo_language_mask;
#define PG_ERROR(n) ((int32_t)(UINT32_C(0x80b20000)|(n)))
static void playgo_stop(const char *reason) {
    fprintf(stderr,"STOP: PlayGo %s\n",reason); fflush(stderr); ExitProcess(26);
}
static uint16_t pg16(const unsigned char *p) { uint16_t v; memcpy(&v,p,2); return v; }
static uint32_t pg32(const unsigned char *p) { uint32_t v; memcpy(&v,p,4); return v; }
static int pg_range(uint32_t off,uint32_t len,uint32_t size) { return off<=size && len<=size-off; }
static void playgo_metadata_read(PlayGoMetadata *m,const wchar_t *variable) {
    wchar_t path[4096]; DWORD n=GetEnvironmentVariableW(variable,path,4096);
    if (!n || n>=4096) playgo_stop("requires explicit SECOND_SON_BASE_PLAYGO and SECOND_SON_PATCH_PLAYGO chunk.dat paths");
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (f==INVALID_HANDLE_VALUE) playgo_stop("cannot read configured chunk metadata");
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f,&size) || size.QuadPart<256 || size.QuadPart>1048576) playgo_stop("invalid metadata length");
    m->size=(uint32_t)size.QuadPart; m->bytes=malloc(m->size);
    if (!m->bytes) playgo_stop("metadata allocation failed");
    DWORD got=0;
    if (!ReadFile(f,m->bytes,m->size,&got,NULL) || got!=m->size) playgo_stop("short metadata read");
    CloseHandle(f);
    unsigned char *b=m->bytes;
    uint16_t images=pg16(b+8),chunks=pg16(b+10),micros=pg16(b+12),scenarios=pg16(b+14);
    if (pg32(b)!=0x6f676c70 || (pg16(b+4)!=0 && pg16(b+4)!=0x3000) || pg16(b+6)!=0 ||
        images!=1 || !chunks || chunks>1000 || !micros || micros>8000 || !scenarios || scenarios>32 ||
        pg32(b+16)!=m->size || pg16(b+20)>=scenarios) playgo_stop("unsupported or malformed metadata header");
    uint32_t off[8],len[8];
    for (unsigned i=0;i<8;++i) {
        off[i]=pg32(b+192+8*i); len[i]=pg32(b+196+8*i);
        if (off[i]<256 || !pg_range(off[i],len[i],m->size)) playgo_stop("metadata section outside file");
        for (unsigned j=0;j<i;++j) if (len[i] && len[j] && off[i]<off[j]+len[j] && off[j]<off[i]+len[i])
            playgo_stop("overlapping metadata sections");
    }
    if (len[0]!=(uint32_t)chunks*32 || len[3]!=(uint32_t)micros*16 || len[4]!=(uint32_t)scenarios*32)
        playgo_stop("metadata record count mismatch");
    for (unsigned i=0;i<chunks;++i) {
        unsigned char *a=b+off[0]+i*32;
        uint32_t ids=pg32(a+24),label=pg32(a+28); uint16_t count=pg16(a+14);
        if (!count || !pg_range(ids,(uint32_t)count*2,len[1]) || label>=len[2] || !memchr(b+off[2]+label,0,len[2]-label))
            playgo_stop("invalid chunk references");
        for (unsigned j=0;j<count;++j) if (pg16(b+off[1]+ids+j*2)>=micros) playgo_stop("invalid microchunk ID");
    }
    for (unsigned i=0;i<scenarios;++i) {
        unsigned char *a=b+off[4]+i*32;
        uint16_t initial=pg16(a+20),count=pg16(a+22); uint32_t ids=pg32(a+24),label=pg32(a+28);
        if (initial>count || !pg_range(ids,(uint32_t)count*2,len[5]) || label>=len[6] || !memchr(b+off[6]+label,0,len[6]-label))
            playgo_stop("invalid scenario references");
        for (unsigned j=0;j<count;++j) if (pg16(b+off[5]+ids+j*2)>=chunks) playgo_stop("invalid scenario chunk ID");
    }
    m->chunks=chunks;
}
static int32_t __attribute__((sysv_abi)) guest_sysmodule_load(uint16_t id) {
    if (id!=0x83) { fprintf(stderr,"STOP: unsupported sceSysmoduleLoadModule(0x%x)\n",id); fflush(stderr); ExitProcess(20); }
    playgo_loaded=1; puts("HOST: loaded scoped PlayGo lifecycle service (availability unsupported)"); fflush(stdout); restore_guest_fs(); return 0;
}
static int32_t __attribute__((sysv_abi)) guest_playgo_initialize(const PlayGoInit *p) {
    if (!p || !p->buffer) return PG_ERROR(10);
    if (p->size<0x200000) return PG_ERROR(11);
    if (p->reserved) return PG_ERROR(4);
    if (playgo_initialized) return PG_ERROR(6);
    if (!playgo_loaded) playgo_stop("initialization before module load");
    playgo_metadata_read(&playgo_metadata[0],L"SECOND_SON_BASE_PLAYGO");
    playgo_metadata_read(&playgo_metadata[1],L"SECOND_SON_PATCH_PLAYGO");
    if (memcmp(playgo_metadata[0].bytes+64,playgo_metadata[1].bytes+64,128) || playgo_metadata[0].chunks!=playgo_metadata[1].chunks)
        playgo_stop("base/patch metadata identity or chunk domain mismatch");
    playgo_initialized=1;
    printf("HOST: PlayGo initialized; base chunks=%u patch chunks=%u; availability unknown\n",playgo_metadata[0].chunks,playgo_metadata[1].chunks); fflush(stdout); restore_guest_fs(); return 0;
}
static int32_t __attribute__((sysv_abi)) guest_playgo_open(uint32_t *handle,const void *p) {
    if (!handle) return PG_ERROR(10);
    if (p) return PG_ERROR(4);
    if (!playgo_initialized) return PG_ERROR(5);
    *handle=1; playgo_opened=1; puts("HOST: PlayGo opened handle=1"); fflush(stdout); restore_guest_fs(); return 0;
}
static int32_t __attribute__((sysv_abi)) guest_playgo_close(uint32_t handle) {
    if (handle!=1) return PG_ERROR(9);
    if (!playgo_initialized) return PG_ERROR(5);
    if (!playgo_opened) return PG_ERROR(9);
    playgo_opened=0; return 0;
}
static int32_t __attribute__((sysv_abi)) guest_playgo_language(uint32_t handle,uint64_t mask) {
    if (handle!=1) return PG_ERROR(9);
    if (!playgo_initialized) return PG_ERROR(5);
    if (!playgo_opened) return PG_ERROR(9);
    playgo_language_mask=mask; return 0;
}
static int32_t __attribute__((sysv_abi)) guest_playgo_get_locus(uint32_t handle,const uint16_t *chunk_ids,uint32_t count,int8_t *locus_out) {
    if (handle!=1) return PG_ERROR(9);
    if (!playgo_initialized) return PG_ERROR(5);
    if (!playgo_opened) return PG_ERROR(9);
    if (!chunk_ids || !locus_out) return PG_ERROR(10);
    if (!count || count>1000) return PG_ERROR(4);
    for (uint32_t i=0;i<count;++i) {
        uint16_t id=chunk_ids[i];
        if (id>=playgo_metadata[0].chunks) { locus_out[i]=0; return PG_ERROR(12); }
        /* Chunk 0 (bootstrap) is pending due to missing patch build marker.
         * Chunks 1..70 are verified present and readable in Image0/Sc0. */
        locus_out[i]=(id==0) ? 0 : 3;
    }
    printf("HOST: PlayGoGetLocus count=%u (chunk 0=pending, 1..%u=ready)\n",count,playgo_metadata[0].chunks-1);
    fflush(stdout);
    restore_guest_fs();
    return 0;
}
static int32_t __attribute__((sysv_abi)) guest_playgo_terminate(void) {
    if (!playgo_initialized) return PG_ERROR(5);
    for (unsigned i=0;i<2;++i) { free(playgo_metadata[i].bytes); memset(&playgo_metadata[i],0,sizeof(playgo_metadata[i])); }
    playgo_initialized=playgo_opened=0; playgo_language_mask=0; restore_guest_fs(); return 0;
}
