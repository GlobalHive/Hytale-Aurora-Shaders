# Installs Aurora Shaders into Hytale (Install.bat runs this):
#   - finds the game (running Hytale, the last install, the launcher's default folder, or asks)
#   - pulls the game's own shaders out of HytaleClient.exe (tools\prepare_shaders.ps1)
#   - copies opengl32.dll next to HytaleClient.exe and links Client\shaderpack to this folder,
#     so settings changed in the game menu are kept here
#
#   install.ps1                 install (prebuilt bin\opengl32.dll, or builds it if missing)
#   install.ps1 -Build          build the DLL from source first (tools\zig is fetched if needed)
#   install.ps1 -Uninstall      remove the DLL and the link again
#   install.ps1 -ClientDir "<...>\game\latest\Client"
param(
    [string]$ClientDir,
    [switch]$Build,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$configFile = Join-Path $root 'install.config'

function Test-Client($dir) { $dir -and (Test-Path (Join-Path $dir 'HytaleClient.exe')) }

function Find-Client {
    $running = Get-Process HytaleClient -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($running -and $running.Path) { return Split-Path $running.Path }
    if (Test-Path $configFile) {
        $saved = (Get-Content $configFile -Raw).Trim()
        if (Test-Client $saved) { return $saved }
    }
    $default = Join-Path $env:APPDATA 'Hytale\install\release\package\game\latest\Client'
    if (Test-Client $default) { return $default }

    Write-Host 'Hytale was not found in its default folder. Pick the folder Hytale is installed in.'
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your Hytale folder (the one that contains "install")'
    if ($dialog.ShowDialog() -ne 'OK') { throw 'No folder selected.' }
    $exe = Get-ChildItem $dialog.SelectedPath -Recurse -Filter HytaleClient.exe -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $exe) { throw "HytaleClient.exe not found under $($dialog.SelectedPath)." }
    return $exe.DirectoryName
}

if (-not (Test-Client $ClientDir)) { $ClientDir = Find-Client }
Set-Content -LiteralPath $configFile -Value $ClientDir -Encoding ascii
Write-Host "Hytale: $ClientDir"

while (Get-Process HytaleClient -ErrorAction SilentlyContinue) {
    Read-Host 'Hytale is running. Close the game, then press Enter'
}

$dll = Join-Path $ClientDir 'opengl32.dll'
$link = Join-Path $ClientDir 'shaderpack'
if (Test-Path $link) {
    $item = Get-Item -LiteralPath $link -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { $item.Delete() }
    else { Remove-Item -LiteralPath $link -Recurse -Force }
}
if ($Uninstall) {
    if (Test-Path $dll) { Remove-Item -LiteralPath $dll -Force }
    Write-Host 'Aurora Shaders removed. Hytale is back to its own shaders.'
    return
}

# The DLL: built from source on request, else the prebuilt one, else a local build.
$prebuilt = Join-Path $root 'bin\opengl32.dll'
$local = Join-Path $root 'build\opengl32.dll'
if ($Build -or (-not (Test-Path $prebuilt) -and -not (Test-Path $local))) {
    & (Join-Path $root 'build.ps1')
    $source = $local
} elseif (Test-Path $prebuilt) {
    $source = $prebuilt
} else {
    $source = $local
}

& (Join-Path $root 'tools\prepare_shaders.ps1') -Exe (Join-Path $ClientDir 'HytaleClient.exe')

Copy-Item -LiteralPath $source -Destination $dll -Force
New-Item -ItemType Junction -Path $link -Target (Join-Path $root 'shaderpack') | Out-Null
Write-Host ''
Write-Host 'Aurora Shaders installed. Start Hytale; in game, Home opens the options and Pause'
Write-Host 'switches the shaders off and on. Keep this folder: the game loads the pack from here.'
