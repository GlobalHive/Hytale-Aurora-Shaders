$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache\global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-cache\local'
Push-Location $root
try {
    & tools\zig\zig.exe cc -target x86_64-windows-gnu -O2 -UNDEBUG -municode -Ihook tools\denoiser_test\test.c hook\gl_stubs.c -lgdi32 -luser32 -o build\denoiser_test.exe
    if ($LASTEXITCODE -ne 0) { throw "Denoiser GPU test compilation failed ($LASTEXITCODE)" }
} finally { Pop-Location }
