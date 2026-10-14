/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only /app0 overlay. No guest writes or arbitrary host paths are exposed. */
static HANDLE guest_files[256];
static wchar_t asset_roots[2][2048];
static int assets_ready;
static int32_t kernel_file_error(DWORD error) {
    unsigned code;
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: code=2; break;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: code=13; break;
    case ERROR_TOO_MANY_OPEN_FILES: code=24; break;
    case ERROR_NOACCESS: case ERROR_INVALID_USER_BUFFER: code=14; break;
    case ERROR_READ_FAULT: case ERROR_CRC: case ERROR_IO_DEVICE: code=5; break;
    default: fprintf(stderr,"STOP: unmapped Windows file error %lu\n",error); fflush(stderr); ExitProcess(25);
    }
    return (int32_t)(UINT32_C(0x80020000)|code);
}
static void configure_assets(void) {
    if (assets_ready) return;
    const wchar_t *variables[]={L"SECOND_SON_PATCH_IMAGE0",L"SECOND_SON_BASE_IMAGE0"};
    for (unsigned i=0;i<2;++i) {
        wchar_t path[2048];
        DWORD n=GetEnvironmentVariableW(variables[i],path,2048);
        if (!n || n>=2048) fail("set SECOND_SON_PATCH_IMAGE0 and SECOND_SON_BASE_IMAGE0 to extracted Image0 directories");
        HANDLE root=CreateFileW(path,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
        if (root==INVALID_HANDLE_VALUE) fail("cannot open configured asset root");
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(root,&info) || !(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) fail("asset root is not a directory");
        n=GetFinalPathNameByHandleW(root,asset_roots[i],2048,FILE_NAME_NORMALIZED);
        CloseHandle(root);
        if (!n || n>=2047) fail("cannot resolve configured asset root");
        if (asset_roots[i][n-1]!=L'\\') { asset_roots[i][n++]=L'\\'; asset_roots[i][n]=0; }
    }
    assets_ready=1;
}
static int32_t __attribute__((sysv_abi)) guest_open(const char *path,int flags,uint16_t mode) {
    (void)mode;
    if (flags) { fprintf(stderr,"STOP: unsupported sceKernelOpen flags 0x%x\n",flags); fflush(stderr); ExitProcess(25); }
    if (!path || strncmp(path,"/app0/",6)) fail("unsupported guest mount in sceKernelOpen");
    const char *relative=path+6;
    size_t len=strnlen(relative,1024);
    if (!len || len>=1024) return (int32_t)UINT32_C(0x80020016);
    const char *component=relative;
    for (size_t j=0;j<=len;++j) {
        unsigned char ch=(unsigned char)relative[j];
        if (ch=='\\' || ch==':' || ch=='*' || ch=='?' || ch=='"' || ch=='<' || ch=='>' || ch=='|' || (ch && ch<32))
            return (int32_t)UINT32_C(0x8002000d);
        if (!ch || ch=='/') {
            size_t n=relative+j-component;
            if (!n || (n==1 && component[0]=='.') || (n==2 && component[0]=='.' && component[1]=='.') || component[n-1]=='.' || component[n-1]==' ')
                return (int32_t)UINT32_C(0x8002000d);
            component=relative+j+1;
        }
    }
    wchar_t suffix[1024];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative,-1,suffix,1024)) return (int32_t)UINT32_C(0x80020016);
    for (wchar_t *c=suffix;*c;++c) if (*c==L'/') *c=L'\\';
    configure_assets();
    for (unsigned i=0;i<2;++i) {
        wchar_t host[4096];
        if (swprintf(host,4096,L"%ls%ls",asset_roots[i],suffix)<0) fail("asset path exceeds host limit");
        HANDLE file=CreateFileW(host,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if (file==INVALID_HANDLE_VALUE) {
            DWORD error=GetLastError();
            if (i==0 && (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)) continue;
            return kernel_file_error(error);
        }
        wchar_t final[4096];
        DWORD n=GetFinalPathNameByHandleW(file,final,4096,FILE_NAME_NORMALIZED);
        size_t rootlen=wcslen(asset_roots[i]);
        if (!n || n>=4096 || n<rootlen || CompareStringOrdinal(final,(int)rootlen,asset_roots[i],(int)rootlen,TRUE)!=CSTR_EQUAL) {
            CloseHandle(file); return (int32_t)UINT32_C(0x8002000d);
        }
        for (unsigned fd=3;fd<256;++fd) if (!guest_files[fd]) {
            guest_files[fd]=file;
            printf("HOST: opened %s from %s, fd=%u\n",path,i ? "base" : "patch",fd); fflush(stdout);
            return (int32_t)fd;
        }
        CloseHandle(file); return (int32_t)UINT32_C(0x80020018);
    }
    return (int32_t)UINT32_C(0x80020002);
}

static int64_t __attribute__((sysv_abi)) guest_lseek(int fd,int64_t offset,int whence) {
    if (fd<3 || fd>=256 || !guest_files[fd]) return (int32_t)UINT32_C(0x80020009);
    if (whence<0 || whence>2) return (int32_t)UINT32_C(0x80020016);
    LARGE_INTEGER distance,position; distance.QuadPart=offset;
    if (!SetFilePointerEx(guest_files[fd],distance,&position,(DWORD)whence)) {
        if (GetLastError()==ERROR_NEGATIVE_SEEK) return (int32_t)UINT32_C(0x80020016);
        return kernel_file_error(GetLastError());
    }
    printf("HOST: seek fd=%d offset=%" PRId64 " whence=%d -> %lld\n",fd,offset,whence,(long long)position.QuadPart); fflush(stdout);
    return position.QuadPart;
}

static int64_t __attribute__((sysv_abi)) guest_read(int fd,void *buffer,uint64_t count) {
    if (fd<3 || fd>=256 || !guest_files[fd]) return (int32_t)UINT32_C(0x80020009);
    if (count>INT64_MAX) return (int32_t)UINT32_C(0x80020016);
    if (!count) return 0;
    DWORD got=0;
    DWORD request=count>UINT32_MAX ? UINT32_MAX : (DWORD)count;
    if (!ReadFile(guest_files[fd],buffer,request,&got,NULL)) return kernel_file_error(GetLastError());
    printf("HOST: read fd=%d requested=%" PRIu64 " -> %lu bytes\n",fd,count,got); fflush(stdout);
    return got;
}

static int32_t __attribute__((sysv_abi)) guest_close(int fd) {
    if (fd<3 || fd>=256 || !guest_files[fd]) return (int32_t)UINT32_C(0x80020009);
    if (!CloseHandle(guest_files[fd])) return kernel_file_error(GetLastError());
    guest_files[fd]=NULL;
    printf("HOST: closed fd=%d\n",fd); fflush(stdout);
    return 0;
}
