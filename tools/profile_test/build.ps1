$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache\global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-cache\local'
Push-Location $root
try {
    & tools\zig\zig.exe cc -target x86_64-windows-gnu -O2 -municode -DAU_HAS_DLSS=1 `
        -Ihook -Ithird_party\DLSS\include -Ithird_party\vulkan\include `
        tools\profile_test\test.c hook\gl_stubs.c hook\aud_dlss.c -lgdi32 -luser32 -ladvapi32 -o build\profile_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Profile test compilation failed.' }
    $fixture = Join-Path $root ('build\validation\profiles-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path (Join-Path $fixture 'include') -Force | Out-Null
    Copy-Item shaderpack\options.txt $fixture
    Copy-Item shaderpack\include\AU_Settings.glsl (Join-Path $fixture 'include')
    & build\profile_test.exe $fixture
    if ($LASTEXITCODE -ne 0) { throw 'Profile tests failed.' }
} finally { Pop-Location }
