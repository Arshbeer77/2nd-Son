/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded initializer and entry trace. No host API is silently treated as successful.
 */
#include <cpuid.h>
#include <bcrypt.h>
uintptr_t runtime_memory_resolve(const char *name);
uintptr_t runtime_mutex_resolve(const char *name);
uintptr_t runtime_thread_resolve(const char *name);
void runtime_set_main_tls(const void *data,uint64_t filesz,uint64_t memsz,uint64_t align);
void runtime_thread_attach_host(const char *name);
void *runtime_thread_get_tcb(void);
void runtime_thread_set_probe_stack(uint64_t size,uint64_t guard);
void *runtime_low_map(size_t size, int prot);
const char *second_son_import_name(const char *encoded);
static unsigned char *probe_image, *probe_traps;
static uint64_t probe_size, probe_count, saved_fs;
static int changed_fs;
static const char *probe_phase="initializers";
void __attribute__((sysv_abi)) restore_guest_fs(void) {
    void *tcb=runtime_thread_get_tcb();
    __asm__ volatile("wrfsbase %0" : : "r"(tcb));
}
static char (*probe_names)[128];
static void *guest_procparam_address;
static void **guest_heap_api;
static uint64_t guest_stack_guard;
/* Layout and state contract from shadPS4 7295d120, libc_internal_memory.cpp.
 * GPL-2.0-or-later, Copyright 2024 shadPS4 Emulator Project. */
typedef struct { uint64_t size; uint32_t flag, segments; uint64_t *mask, *states; } HeapTraceInfo;
static uint64_t heap_trace_mask;
static uint64_t heap_trace_states[64];
static void __attribute__((sysv_abi)) guest_heap_trace(HeapTraceInfo *info) {
    if (!info || info->size != sizeof(*info)) fail("unsupported heap trace structure");
    info->mask=&heap_trace_mask; info->states=heap_trace_states; info->segments=0;
}
static void *__attribute__((sysv_abi)) guest_procparam(void) {
    return guest_procparam_address;
}
static void __attribute__((sysv_abi)) guest_set_heap_api(void **api) {
    guest_heap_api=api;
    puts("HOST: registered application heap API"); fflush(stdout);
}
/* Guest RDTSC instructions run directly. Report that counter's rate, never
 * QueryPerformanceFrequency (which may describe a different Windows clock). */
static uint64_t read_host_tsc(void) {
    unsigned lo,hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo),"=d"(hi) : : "memory");
    return ((uint64_t)hi<<32)|lo;
}
static uint64_t __attribute__((sysv_abi)) guest_tsc_frequency(void) {
    static uint64_t hz;
    if (hz) return hz;
    unsigned a,b,c,d;
    if (!__get_cpuid(0x80000007,&a,&b,&c,&d) || !(d&(1u<<8)))
        fail("host does not advertise invariant TSC");
    if (__get_cpuid_count(0x15,0,&a,&b,&c,&d) && a && b && c) {
        hz=(uint64_t)c*b/a;
    } else {
        LARGE_INTEGER frequency,start,end;
        uint64_t samples[3];
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart<=0)
            fail("cannot calibrate guest TSC against Windows clock");
        for (unsigned i=0;i<3;++i) {
            if (!QueryPerformanceCounter(&start)) fail("cannot read Windows clock");
            uint64_t first=read_host_tsc();
            Sleep(20);
            uint64_t last=read_host_tsc();
            if (!QueryPerformanceCounter(&end) || end.QuadPart<=start.QuadPart || last<=first)
                fail("invalid TSC calibration sample");
            samples[i]=(uint64_t)((long double)(last-first)*frequency.QuadPart/(end.QuadPart-start.QuadPart));
        }
        for (unsigned i=0;i<3;++i) for (unsigned j=i+1;j<3;++j)
            if (samples[j]<samples[i]) { uint64_t t=samples[i]; samples[i]=samples[j]; samples[j]=t; }
        if (samples[2]-samples[0]>samples[1]/100) fail("unstable TSC calibration exceeds one percent");
        hz=samples[1];
    }
    if (hz<1000000 || hz>UINT64_C(10000000000)) fail("implausible host TSC frequency");
    printf("HOST: invariant TSC frequency=%" PRIu64 " Hz\n",hz); fflush(stdout);
    return hz;
}
#include "files.h"
#include "playgo.h"
static uintptr_t probe_resolve(const char *name, uint64_t kind) {
    if (kind==1 && !strcmp(name,"g8cM39EUZ6o#A#B")) return (uintptr_t)guest_sysmodule_load;
    if (kind==1 && !strcmp(name,"ts6GlZOKRrE#a#b")) return (uintptr_t)guest_playgo_initialize;
    if (kind==1 && !strcmp(name,"M1Gma1ocrGE#a#b")) return (uintptr_t)guest_playgo_open;
    if (kind==1 && !strcmp(name,"Uco1I0dlDi8#a#b")) return (uintptr_t)guest_playgo_close;
    if (kind==1 && !strcmp(name,"LosLlHOpNqQ#a#b")) return (uintptr_t)guest_playgo_language;
    if (kind==1 && !strcmp(name,"MPe0EeBGM-E#a#b")) return (uintptr_t)guest_playgo_terminate;
    if (kind==1 && !strcmp(name,"UK2Tl2DWUns#b#T")) return (uintptr_t)guest_close;
    if (kind==1 && !strcmp(name,"Cg4srZ6TKbU#b#T")) return (uintptr_t)guest_read;
    if (kind==1 && !strcmp(name,"oib76F-12fk#b#T")) return (uintptr_t)guest_lseek;
    if (kind==1 && !strcmp(name,"1G3lF1Gg1k8#b#T")) return (uintptr_t)guest_open;
    if (kind==1 && !strcmp(name,"1j3S3n-tTW4#b#T")) return (uintptr_t)guest_tsc_frequency;
    if (kind==2 && !strcmp(name,"f7uOxY9mM1U#b#T")) return (uintptr_t)&guest_stack_guard;
    if (kind==1 && !strcmp(name,"959qrazPIrg#b#T")) return (uintptr_t)guest_procparam;
    if (kind==1 && !strcmp(name,"p5EcQeEeJAE#b#T")) return (uintptr_t)guest_set_heap_api;
    if (kind==1 && !strcmp(name,"NWtTN10cJzE#libSceLibcInternalExt")) return (uintptr_t)guest_heap_trace;
    if (kind==1) {
        uintptr_t address=runtime_memory_resolve(name);
        if (address) return address;
        /* The two games assign different local IDs to the same libkernel identity. */
        if (strlen(name)==15 && !strcmp(name+11,"#b#T")) {
            char reference[16]; memcpy(reference,name,11); memcpy(reference+11,"#p#J",5);
            address=runtime_mutex_resolve(reference);
            if (address) return address;
            const char *label=second_son_import_name(name);
            if (!strncmp(label,"scePthreadAttr",14) || !strcmp(label,"scePthreadSelf") || !strcmp(label,"__error"))
                return runtime_thread_resolve(reference);
            return 0;
        }
        return 0;
    }
    return 0;
}
static void restore_fs(void) {
    if (changed_fs) { __asm__ volatile("wrfsbase %0" : : "r"(saved_fs)); changed_fs=0; }
}
static LONG CALLBACK probe_exception(EXCEPTION_POINTERS *e) {
    restore_fs();
    uint64_t pc=e->ContextRecord->Rip;
    uint64_t address=pc;
    if (e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION && e->ExceptionRecord->NumberParameters>=2)
        address=e->ExceptionRecord->ExceptionInformation[1];
    if (address >= (uintptr_t)probe_traps && address-(uintptr_t)probe_traps < probe_count*4096) {
        uint64_t index=(address-(uintptr_t)probe_traps)/4096;
        fprintf(stderr,"STOP: unsupported import [%" PRIu64 "] %s\n",index,probe_names[index]);
        fprintf(stderr,"Name: %s\n",second_son_import_name(probe_names[index]));
        fprintf(stderr,"Arguments: RDI=0x%llx RSI=0x%llx RDX=0x%llx\n",
                (unsigned long long)e->ContextRecord->Rdi,(unsigned long long)e->ContextRecord->Rsi,
                (unsigned long long)e->ContextRecord->Rdx);
        if (!strcmp(second_son_import_name(probe_names[index]),"sceKernelOpen")) {
            char path[512]; size_t n;
            for (n=0;n<sizeof(path)-1;++n) {
                SIZE_T got;
                if (!ReadProcessMemory(GetCurrentProcess(),(void *)(uintptr_t)(e->ContextRecord->Rdi+n),path+n,1,&got) || got!=1)
                    break;
                if (!path[n]) break;
            }
            path[n]=0;
            fprintf(stderr,"Open path (bounded diagnostic): %s\n",path);
        }
        fprintf(stderr,"Access kind: %" PRIuPTR "; RIP: 0x%" PRIx64 "\n",(uintptr_t)e->ExceptionRecord->ExceptionInformation[0],pc);
        if (IsBadReadPtr((void *)(uintptr_t)e->ContextRecord->Rsp,8)==0) {
            uint64_t ret; memcpy(&ret,(void *)(uintptr_t)e->ContextRecord->Rsp,8);
            if (ret>=(uintptr_t)probe_image && ret-(uintptr_t)probe_image<probe_size)
                fprintf(stderr,"Guest return offset: 0x%" PRIx64 "\n",ret-(uintptr_t)probe_image);
        }
        fflush(stderr); ExitProcess(20);
    }
    fprintf(stderr,"STOP: exception 0x%08lx, RIP 0x%" PRIx64 ", access 0x%" PRIx64 "\n",e->ExceptionRecord->ExceptionCode,pc,address);
    if (pc>=(uintptr_t)probe_image && pc-(uintptr_t)probe_image<probe_size)
        fprintf(stderr,"Guest instruction offset: 0x%" PRIx64 "\n",pc-(uintptr_t)probe_image);
    fflush(stderr); ExitProcess(21);
    return EXCEPTION_CONTINUE_SEARCH;
}
/* One-way Windows x64 -> System V bridge. The argument layout was verified
 * against Second Son entry 0x61dfd0. Stack/TEB technique adapted from GPL-2.0
 * Bloodborne src_probe.c (Mrsuss60, e0a2a27). This probe always ends the process;
 * it never unwinds Windows frames across a guest stack switch. */
void __attribute__((noreturn)) probe_enter(void *entry,void *params,void *finalizer,void *top);
void __attribute__((noreturn)) probe_entry_returned(void) {
    restore_fs(); fputs("STOP: game entry unexpectedly returned\n",stderr);
    fflush(stderr); ExitProcess(24);
}
__asm__(".text\n.globl probe_enter\nprobe_enter:\n"
        "mov %rcx,%rax\nmov %rdx,%rdi\nmov %r8,%rsi\n"
        "mov %r9,%rsp\nand $-16,%rsp\nmov %rsp,%gs:0x8\n"
        "sub $0x800000,%r9\nmov %r9,%gs:0x10\n"
        "xor %ebp,%ebp\ncall *%rax\nsub $32,%rsp\n"
        "call probe_entry_returned\nud2\n");
static void __attribute__((sysv_abi)) probe_finalizer(void) {
    puts("PROBE: process finalizer callback reached"); fflush(stdout);
}
static DWORD WINAPI probe_watchdog(void *unused) {
    (void)unused; Sleep(5000); fprintf(stderr,"STOP: %s probe reached five-second limit\n",probe_phase);
    fflush(stderr); ExitProcess(22); return 0;
}
static void trace_initializers(unsigned char *image, uint64_t size, unsigned char *traps,
                               uint64_t ni, char (*names)[128], Module *mods, uint64_t nm, uint64_t tls[4], int enter_game, uint64_t entry) {
    probe_image=image; probe_size=size; probe_traps=traps; probe_count=ni; probe_names=names;
    MEMORYSTATUSEX memory={0}; memory.dwLength=sizeof(memory);
    if (!GlobalMemoryStatusEx(&memory)) fail("cannot check host memory headroom");
    printf("HOST: available commit headroom: %llu MiB\n",(unsigned long long)(memory.ullAvailPageFile/1048576));
    if (memory.ullAvailPageFile < UINT64_C(8192)*1048576) fail("memory probe needs 8 GiB of available commit headroom");
    if (BCryptGenRandom(NULL,(PUCHAR)&guest_stack_guard,sizeof(guest_stack_guard),BCRYPT_USE_SYSTEM_PREFERRED_RNG)) fail("cannot initialize stack guard");
    guest_stack_guard &= ~UINT64_C(255);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (!AddVectoredExceptionHandler(1,probe_exception)) fail("cannot install probe exception handler");
    unsigned eax,ebx,ecx,edx;
    if (!__get_cpuid_count(7,0,&eax,&ebx,&ecx,&edx) || !(ebx&1)) fail("CPU lacks FSGSBASE for guest TLS");
    if (!CreateThread(NULL,0,probe_watchdog,NULL,0,NULL)) fail("cannot start probe watchdog");
    __asm__ volatile("rdfsbase %0" : "=r"(saved_fs));
    changed_fs=1;
    runtime_set_main_tls(image+tls[0],tls[1],tls[2],tls[3]);
    runtime_thread_attach_host("second-son-initializer-probe");
    typedef int (__attribute__((sysv_abi)) *Init)(uint64_t,void *,void *);
    for (uint64_t m=0; m<nm; ++m) {
        printf("PROBE: entering module %" PRIu64 " initializer at 0x%" PRIx64 "\n",m,mods[m].init); fflush(stdout);
        int result=((Init)(image+mods[m].init))(0,NULL,NULL);
        printf("PROBE: module %" PRIu64 " returned %d\n",m,result); fflush(stdout);
        if (result) { restore_fs(); ExitProcess(23); }
    }
    if (enter_game) {
        enum { STACK_BYTES=8*1024*1024 };
        unsigned char *stack=runtime_low_map(STACK_BYTES+8192,3);
        unsigned char *startup=runtime_low_map(4096,3);
        if (!stack || !startup) fail("cannot allocate guest entry stack/arguments");
        DWORD old;
        if (!VirtualProtect(stack,4096,PAGE_NOACCESS,&old) ||
            !VirtualProtect(stack+4096+STACK_BYTES,4096,PAGE_NOACCESS,&old))
            fail("cannot protect guest stack boundaries");
        uint64_t *params=(uint64_t *)startup;
        memcpy(startup+64,"/app0/eboot.bin",15);
        params[0]=1; params[1]=(uintptr_t)(startup+64); params[2]=0;
        runtime_thread_set_probe_stack(STACK_BYTES,4096);
        probe_phase="game entry";
        printf("PROBE: entering game at 0x%" PRIx64 "; stack=%u bytes; argc=1\n",entry,STACK_BYTES);
        fflush(stdout);
        restore_guest_fs();
        probe_enter(image+entry,params,(void *)probe_finalizer,stack+4096+STACK_BYTES);
    }
    restore_fs();
    puts("PROBE: linked initializers returned; game entry was not executed."); fflush(stdout);
    ExitProcess(0);
}
