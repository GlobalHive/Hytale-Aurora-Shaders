param([switch]$Run)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache\global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-cache\local'
Push-Location $root
try {
    & tools\zig\zig.exe cc -target x86_64-windows-gnu -O2 -UNDEBUG -DAU_HAS_DLSS=1 `
        -Ihook -Ithird_party\DLSS\include -Ithird_party\vulkan\include `
        tools\shader_test\test.c hook\gl_stubs.c hook\aud_dlss.c -lgdi32 -luser32 -ladvapi32 -o build\shader_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Shader compiler harness build failed.' }
    if ($Run) {
        $client = (Get-Content -LiteralPath (Join-Path $root 'install.config') -Raw).Trim()
        & build\shader_test.exe (Join-Path $root 'shaderpack') (Join-Path $client 'SDL3.dll')
        if ($LASTEXITCODE -ne 0) { throw 'A shader variant failed compilation.' }
    }
} finally { Pop-Location }
