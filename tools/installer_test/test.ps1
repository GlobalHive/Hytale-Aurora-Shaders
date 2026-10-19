param([Parameter(Mandatory)][string]$ClientExe)
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$fixture = Join-Path $root ('build\validation\installer-' + [guid]::NewGuid().ToString('N'))
$package = Join-Path $fixture 'package'
$client = Join-Path $fixture 'Client'
$foreign = Join-Path $fixture 'old-shaders'
foreach ($path in @($package, $client, $foreign, (Join-Path $package 'bin'), (Join-Path $package 'tools'),
                    (Join-Path $foreign 'include'))) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
Copy-Item -LiteralPath $ClientExe -Destination (Join-Path $client 'HytaleClient.exe')
Copy-Item -LiteralPath (Join-Path $root 'install.ps1') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'tools\prepare_shaders.ps1') -Destination (Join-Path $package 'tools')
Copy-Item -LiteralPath (Join-Path $root 'tools\build_emitters.ps1') -Destination (Join-Path $package 'tools')
Copy-Item -LiteralPath (Join-Path $root 'tools\build_sss.ps1') -Destination (Join-Path $package 'tools')
Copy-Item -LiteralPath (Join-Path $root 'tools\sss_masks.cs') -Destination (Join-Path $package 'tools')
Copy-Item -LiteralPath (Join-Path $root 'tools\mapchunk') -Destination (Join-Path $package 'tools') -Recurse
$pack = Join-Path $package 'shaderpack'
New-Item -ItemType Directory -Path $pack | Out-Null
foreach ($name in @('include', 'programs', 'textures')) {
    Copy-Item -LiteralPath (Join-Path $root ('shaderpack\' + $name)) -Destination $pack -Recurse
}
Get-ChildItem -LiteralPath (Join-Path $root 'shaderpack') -File | Where-Object { $_.Extension -eq '.txt' } |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $pack }
[IO.File]::WriteAllText((Join-Path $package 'bin\opengl32.dll'), 'new test proxy')
[IO.File]::WriteAllText((Join-Path $client 'opengl32.dll'), 'changed old proxy')
[IO.File]::WriteAllText((Join-Path $foreign 'keep-me.txt'), 'do not remove the junction target')
[IO.File]::WriteAllText((Join-Path $foreign 'include\AU_Settings.glsl'), "#define AU_GI_DENOISER 2`n#define AU_GI_RESOLUTION 1`n#define AU_GI_HISTORY_TAPS 4`n#define AU_SCREEN_BRIGHTNESS 1.25`n")
New-Item -ItemType Directory -Path (Join-Path $foreign 'presets') | Out-Null
$customPreset = "// Aurora preset: Cave`n#define AU_SCREEN_BRIGHTNESS 1.25`n"
[IO.File]::WriteAllText((Join-Path $foreign 'presets\preset-00.glsl'), $customPreset)
New-Item -ItemType Directory -Path (Join-Path $foreign 'sss\Blocks\Foliage\Leaves') -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $foreign 'sss\Blocks\Foliage\Leaves\Oak.png'), 'custom mask fixture')
New-Item -ItemType Junction -Path (Join-Path $client 'shaderpack') -Target $foreign | Out-Null
@{ Owner = 'Aurora Shaders'; InstalledHash = 'stale'; PreviousDll = '.aurora-missing-backup' } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $client '.aurora-install.json')
[IO.File]::WriteAllText((Join-Path $client '.aurora-old-artifact'), 'obsolete')
& (Join-Path $package 'install.ps1') -ClientDir $client -Update -NonInteractive
if ([IO.File]::ReadAllText((Join-Path $client 'opengl32.dll')) -ne 'new test proxy') { throw 'DLL was not replaced.' }
if (-not (Test-Path -LiteralPath (Join-Path $foreign 'keep-me.txt'))) { throw 'Old junction target was modified.' }
if ([IO.File]::ReadAllText((Join-Path $client 'shaderpack\presets\preset-00.glsl')) -ne $customPreset) { throw 'Custom preset was not preserved.' }
if ([IO.File]::ReadAllText((Join-Path $client 'shaderpack\sss\Blocks\Foliage\Leaves\Oak.png')) -ne 'custom mask fixture') { throw 'Custom SSS mask was not preserved.' }
$settings = [IO.File]::ReadAllText((Join-Path $client 'shaderpack\include\AU_Settings.glsl'))
if ($settings -notmatch '#define AU_GI_DENOISER 1\s' -or $settings -notmatch '#define AU_GI_HISTORY_TAPS 9\s' -or
    $settings -notmatch '#define AU_SCREEN_BRIGHTNESS 1.25\s') { throw 'Settings preservation or legacy denoiser migration failed.' }
if (Test-Path -LiteralPath (Join-Path $client '.aurora-old-artifact')) { throw 'Old install debris remained in the game folder.' }
[IO.File]::WriteAllText((Join-Path $client 'opengl32.dll'), 'updated outside the installer')
& (Join-Path $package 'install.ps1') -ClientDir $client -Update -NonInteractive
# A shipped and a local DLL: the newer one is installed.
New-Item -ItemType Directory -Path (Join-Path $package 'build') -Force | Out-Null
$shippedTime = (Get-Item -LiteralPath (Join-Path $package 'bin\opengl32.dll')).LastWriteTimeUtc
$localDll = Join-Path $package 'build\opengl32.dll'
[IO.File]::WriteAllText($localDll, 'local build')
(Get-Item -LiteralPath $localDll).LastWriteTimeUtc = $shippedTime.AddMinutes(5)
& (Join-Path $package 'install.ps1') -ClientDir $client -Update -NonInteractive
if ([IO.File]::ReadAllText((Join-Path $client 'opengl32.dll')) -ne 'local build') { throw 'A newer local build was not installed.' }
(Get-Item -LiteralPath $localDll).LastWriteTimeUtc = $shippedTime.AddMinutes(-5)
& (Join-Path $package 'install.ps1') -ClientDir $client -Update -NonInteractive
if ([IO.File]::ReadAllText((Join-Path $client 'opengl32.dll')) -ne 'new test proxy') { throw 'An older local build replaced the shipped DLL.' }
& (Join-Path $package 'install.ps1') -ClientDir $client -Uninstall -NonInteractive
$remaining = @(Get-ChildItem -LiteralPath $client -Force)
if ($remaining.Count -ne 1 -or $remaining[0].Name -ne 'HytaleClient.exe') { throw 'Uninstall left shader files or links in the game folder.' }
if (-not (Test-Path -LiteralPath (Join-Path $foreign 'keep-me.txt'))) { throw 'Uninstall followed a junction into user shaders.' }
Write-Host 'PASS: clean update, changed DLL, newer of shipped and local DLL, missing backup, settings migration, preserved link target, and complete uninstall.'
