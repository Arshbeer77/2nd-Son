param([string]$Gcc = $env:SECOND_SON_GCC)
$ErrorActionPreference = 'Stop'
if (-not $Gcc) { $Gcc = 'gcc' }
$compiler = (Get-Command $Gcc -ErrorAction Stop).Source
$target = & $compiler -dumpmachine
if ($LASTEXITCODE -or $target -notmatch '^x86_64-.*(mingw|windows)') {
    throw "A 64-bit Windows MinGW GCC is required; compiler target was: $target"
}
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Force outputs | Out-Null
    & $compiler -std=c11 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections '-Wl,--gc-sections' -o outputs/second-son-loader-check.exe loader.c resolve.c vendor/runtime_memory.c vendor/runtime_mutex.c vendor/runtime_thread.c vendor/win32_memory.c vendor/runtime_host.c vendor/win32_compat.c -lbcrypt -lws2_32
    if ($LASTEXITCODE) { throw 'Compilation failed' }
} finally { Pop-Location }
