$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$zig = Join-Path $root 'tools\zig\zig.exe'
if (-not (Test-Path -LiteralPath $zig)) { & (Join-Path $root 'build.ps1') }
if (-not (Test-Path (Join-Path $root 'third_party\Streamline\include\sl_dlss_g.h'))) {
    & (Join-Path $root 'tools\prepare_streamline.ps1')
}
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root 'build\zig-cache\global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root 'build\zig-cache\local'
Push-Location $root
try {
    & $zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -fno-exceptions -fno-rtti -municode `
        -Ithird_party\Streamline\include -Ithird_party\vulkan\include `
        tools\framegen_test\framegen_test.cpp hook\aud_framegen.cpp -o build\framegen_test.exe `
        -lopengl32 -lgdi32 -luser32 -lwintrust -lcrypt32
    if ($LASTEXITCODE -ne 0) { throw "Frame Generation test compilation failed ($LASTEXITCODE)" }
} finally { Pop-Location }
Write-Host "Built $(Join-Path $root 'build\framegen_test.exe')"
