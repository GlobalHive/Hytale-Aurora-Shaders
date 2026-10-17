[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$ClientDir,
    [switch]$Build,
    [switch]$Uninstall,
    [switch]$Update,
    [switch]$ChooseLocation,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$configFile = Join-Path $root 'install.config'
$sourcePack = Join-Path $root 'shaderpack'

function Save-OldClientFiles([string]$BackupDirectory) {
    New-Item -ItemType Directory -Path $BackupDirectory -Force | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $ClientDir -Force) {
        if ($item.Name -notlike '.aurora-*' -and $item.Name -notlike 'shaderpack.aurora-*') { continue }
        if ($item.Name -eq '.aurora-install.json') { continue }
        $path = Client-Child $item.Name
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { $item.Delete() }
        else { Move-Item -LiteralPath $path -Destination (Join-Path $BackupDirectory $item.Name) }
    }
}

function Resolve-ClientPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    $candidate = [Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"').Trim("'"))
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        if ([IO.Path]::GetFileName($candidate) -ine 'HytaleClient.exe') { return $null }
        return (Get-Item -LiteralPath $candidate).Directory.FullName
    }
    foreach ($suffix in @('', 'Client', 'game\latest\Client', 'release\package\game\latest\Client',
                           'install\release\package\game\latest\Client', 'Hytale\install\release\package\game\latest\Client')) {
        $directory = if ($suffix) { Join-Path $candidate $suffix } else { $candidate }
        if ((Test-Path -LiteralPath (Join-Path $directory 'HytaleClient.exe') -PathType Leaf) -or
            ($Uninstall -and (Test-Path -LiteralPath (Join-Path $directory '.aurora-install.json')))) {
            return (Get-Item -LiteralPath $directory).FullName
        }
    }
    return $null
}

function Find-Client {
    $running = Get-Process HytaleClient -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($running -and $running.Path) { return Resolve-ClientPath $running.Path }
    if (Test-Path -LiteralPath $configFile) {
        $saved = Resolve-ClientPath (Get-Content -LiteralPath $configFile -Raw)
        if ($saved) { return $saved }
    }
    return Resolve-ClientPath (Join-Path $env:APPDATA 'Hytale')
}

function Select-Client([string]$Detected) {
    while ($true) {
        Write-Host ''
        if ($Detected) { Write-Host "1. Use detected installation: $Detected" }
        Write-Host '2. Browse for HytaleClient.exe (any drive)'
        Write-Host '3. Paste an install folder or HytaleClient.exe path'
        Write-Host 'Q. Cancel'
        $choice = Read-Host 'Choose installation location'
        $selected = $null
        switch ($choice.ToUpperInvariant()) {
            '1' { if ($Detected) { return $Detected } }
            '2' {
                Add-Type -AssemblyName System.Windows.Forms
                $dialog = New-Object System.Windows.Forms.OpenFileDialog
                try {
                    $dialog.Title = 'Select HytaleClient.exe on any drive'
                    $dialog.Filter = 'Hytale client|HytaleClient.exe'
                    $dialog.CheckFileExists = $true
                    $dialog.RestoreDirectory = $true
                    if ($Detected) { $dialog.InitialDirectory = $Detected }
                    if ($dialog.ShowDialog() -eq 'OK') { $selected = Resolve-ClientPath $dialog.FileName }
                } finally { $dialog.Dispose() }
            }
            '3' { $selected = Resolve-ClientPath (Read-Host 'Full folder or executable path, for example D:\Games\Hytale') }
            'Q' { throw 'Installation cancelled. No files changed.' }
        }
        if ($selected) { return $selected }
        Write-Host 'No valid client selected. Select HytaleClient.exe or the folder containing its install directory.'
    }
}

function Client-Child([string]$Name) {
    if ([IO.Path]::GetFileName($Name) -ne $Name) { throw 'Unsafe install backup name.' }
    $path = [IO.Path]::GetFullPath((Join-Path $ClientDir $Name))
    $parent = [IO.Path]::GetDirectoryName($path)
    if ($parent -ine $ClientDir) { throw 'Install path escaped the client folder.' }
    return $path
}

function Restore-Settings([string]$OldPath, [string]$NewPath) {
    if (-not (Test-Path -LiteralPath $OldPath)) { return }
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $OldPath) {
        if ($line -match '^#define\s+(AU_[A-Z0-9_]+)\s+(-?\d+(?:\.\d+)?)[uUfF]?\s*(?://.*)?$') { $values[$matches[1]] = $matches[2] }
    }
    if (Select-String -LiteralPath $OldPath -Pattern '^#define\s+AU_GI_RESOLUTION\s+' -Quiet) {
        $values['AU_GI_DENOISER'] = '1'
        $values['AU_GI_HISTORY_TAPS'] = '9'
    }
    $settings = foreach ($line in Get-Content -LiteralPath $NewPath) {
        if ($line -match '^(#define\s+)(AU_[A-Z0-9_]+)(\s+)(-?\d+(?:\.\d+)?)(.*)$' -and $values.ContainsKey($matches[2])) {
            $matches[1] + $matches[2] + $matches[3] + $values[$matches[2]] + $matches[5]
        } else { $line }
    }
    $settings | Set-Content -LiteralPath $NewPath -Encoding UTF8
}

if (-not $NonInteractive -and -not $Uninstall -and -not $Update) {
    Write-Host '1. Clean install / reinstall (preserve settings)'
    Write-Host '2. Update shaders for a new game version (clean reinstall)'
    Write-Host '3. Uninstall completely'
    switch (Read-Host 'Choose action [1]') {
        '2' { $Update = $true }
        '3' { $Uninstall = $true }
        '' { }
        '1' { }
        default { throw 'Cancelled. No files changed.' }
    }
}

if ($ClientDir) {
    $resolved = Resolve-ClientPath $ClientDir
    if (-not $resolved) { throw 'ClientDir is not a Hytale client or installation folder.' }
    $ClientDir = $resolved
} else {
    $detected = Find-Client
    if ($NonInteractive) {
        if (-not $detected -or $ChooseLocation) { throw 'Specify -ClientDir for a non-interactive installation.' }
        $ClientDir = $detected
    } elseif ($Uninstall -and $detected -and -not $ChooseLocation) {
        $ClientDir = $detected
    } else {
        $ClientDir = Select-Client $detected
    }
}
$ClientDir = [IO.Path]::GetFullPath($ClientDir)
if ($ClientDir.Length -gt 3) { $ClientDir = $ClientDir.TrimEnd('\') }
$packageDirectory = [IO.Path]::GetFullPath($root).TrimEnd('\')
if ($packageDirectory -ieq $ClientDir -or $packageDirectory.StartsWith($ClientDir + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Keep the Aurora package outside the game client folder.'
}
Write-Host "Hytale client: $ClientDir"
if (-not $PSCmdlet.ShouldProcess($ClientDir, $(if ($Uninstall) { 'Uninstall Aurora Shaders' } else { 'Install Aurora Shaders' }))) { return }
foreach ($process in Get-Process HytaleClient -ErrorAction SilentlyContinue) {
    if (-not $process.Path -or [IO.Path]::GetDirectoryName($process.Path) -ieq $ClientDir) {
        throw 'Close this Hytale instance before installing or uninstalling. No files changed.'
    }
}

$dll = Client-Child 'opengl32.dll'
$link = Client-Child 'shaderpack'
$receiptPath = Client-Child '.aurora-install.json'
$receipt = $null
if (Test-Path -LiteralPath $receiptPath) {
    try { $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json }
    catch { Write-Warning 'Old Aurora receipt is damaged; cleaning the known Aurora paths.' }
}
if (Test-Path -LiteralPath $dll) {
    try { $probe = [IO.File]::Open($dll, 'Open', 'ReadWrite', 'None'); $probe.Dispose() }
    catch { throw "The game opengl32.dll is locked or not writable. Close Hytale and retry: $($_.Exception.Message)" }
}
if ($receipt -and $receipt.PreviousDll) { [void](Client-Child $receipt.PreviousDll) }
if ($receipt -and $receipt.PreviousPack) { [void](Client-Child $receipt.PreviousPack) }
$existingPack = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
$isLink = $existingPack -and ($existingPack.Attributes -band [IO.FileAttributes]::ReparsePoint)
$linkedHere = $isLink -and (@($existingPack.Target)[0] -ieq $sourcePack)
$owned = $linkedHere -or ($receipt -and $receipt.Owner -eq 'Aurora Shaders')
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'

if ($Uninstall) {
    $recovery = Join-Path $root ('backups\uninstalled-' + $stamp)
    New-Item -ItemType Directory -Path $recovery -Force | Out-Null
    if (Test-Path -LiteralPath $dll) {
        Move-Item -LiteralPath $dll -Destination (Join-Path $recovery 'opengl32.dll')
    }
    if ($existingPack) {
        if ($isLink) { $existingPack.Delete() }
        else {
            $retained = Join-Path $recovery 'shaderpack'
            Move-Item -LiteralPath $link -Destination $retained
            Write-Host "Settings and custom shaders retained at: $retained"
        }
    }
    Save-OldClientFiles $recovery
    if (Test-Path -LiteralPath $receiptPath) { Move-Item -LiteralPath $receiptPath -Destination (Join-Path $recovery 'install-receipt.json') }
    if ((Test-Path -LiteralPath $configFile) -and (Resolve-ClientPath (Get-Content -LiteralPath $configFile -Raw)) -ieq $ClientDir) {
        Remove-Item -LiteralPath $configFile
    }
    Write-Host "Aurora files and links removed from the game. Recovery files are outside the game folder: $recovery"
    return
}

$prebuilt = Join-Path $root 'bin\opengl32.dll'
$local = Join-Path $root 'build\opengl32.dll'
if ($Build -or (-not (Test-Path -LiteralPath $prebuilt) -and -not (Test-Path -LiteralPath $local))) {
    & (Join-Path $root 'build.ps1')
    $source = $local
} elseif (Test-Path -LiteralPath $prebuilt) { $source = $prebuilt }
else { $source = $local }
& (Join-Path $root 'tools\prepare_shaders.ps1') -Exe (Join-Path $ClientDir 'HytaleClient.exe')

$rollbackDll = Client-Child ".aurora-dll-rollback.$stamp"
$rollbackPack = Client-Child "shaderpack.aurora-backup.$stamp"
$previousDllName = if ($receipt -and $receipt.PreviousDll) { $receipt.PreviousDll } else { $null }
$previousPackName = if ($receipt -and $receipt.PreviousPack) { $receipt.PreviousPack } else { $null }
$packMoved = $false
$dllSaved = $false
$newPackCreated = $false
$oldReceipt = if (Test-Path -LiteralPath $receiptPath) { [IO.File]::ReadAllBytes($receiptPath) } else { $null }
$oldConfig = if (Test-Path -LiteralPath $configFile) { [IO.File]::ReadAllBytes($configFile) } else { $null }
$metadataChanged = $false
try {
    if (Test-Path -LiteralPath $dll) {
        Copy-Item -LiteralPath $dll -Destination $rollbackDll
        $dllSaved = $true
        if (-not $owned -and -not $previousDllName) { $previousDllName = [IO.Path]::GetFileName($rollbackDll) }
        Remove-Item -LiteralPath $dll
    }
    if ($existingPack) {
        Move-Item -LiteralPath $link -Destination $rollbackPack
        $packMoved = $true
        if (-not $owned -and -not $previousPackName) { $previousPackName = [IO.Path]::GetFileName($rollbackPack) }
    }
    $packMode = 'Junction'
    try {
        New-Item -ItemType Junction -Path $link -Target $sourcePack -ErrorAction Stop | Out-Null
        $newPackCreated = $true
    } catch {
        Write-Host 'This drive does not support the junction. Installing a local shaderpack copy instead.'
        $packMode = 'Copy'
        if (Test-Path -LiteralPath $link) {
            $newPackCreated = $true
            $partial = Get-Item -LiteralPath $link -Force
            if ($partial.Attributes -band [IO.FileAttributes]::ReparsePoint) { $partial.Delete() }
            elseif (Get-ChildItem -LiteralPath $link -Force) { throw 'Unexpected files in the new shaderpack folder; not overwritten.' }
        }
        New-Item -ItemType Directory -Path $link -Force | Out-Null
        $newPackCreated = $true
        Get-ChildItem -LiteralPath $sourcePack -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $link -Recurse }
    }
    if ($owned -and $packMoved -and -not $linkedHere) {
        Restore-Settings (Join-Path $rollbackPack 'include\AU_Settings.glsl') (Join-Path $link 'include\AU_Settings.glsl')
        $previousSSS = Join-Path $rollbackPack 'sss'
        $sssDestination = Join-Path $link 'sss'
        $copiedSSS = $false
        if (Test-Path -LiteralPath $previousSSS -PathType Container) {
            foreach ($mask in Get-ChildItem -LiteralPath $previousSSS -Recurse -File -Filter '*.png') {
                if (-not $mask.FullName.StartsWith($previousSSS + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'SSS mask path escaped its folder.' }
                $relativeMask = $mask.FullName.Substring($previousSSS.Length + 1)
                $destination = Join-Path $sssDestination $relativeMask
                if (-not (Test-Path -LiteralPath $destination)) {
                    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
                    Copy-Item -LiteralPath $mask.FullName -Destination $destination
                    $copiedSSS = $true
                }
            }
        }
        $assets = Join-Path (Split-Path $ClientDir -Parent) 'Assets.zip'
        if ($copiedSSS -and (Test-Path -LiteralPath $assets -PathType Leaf)) {
            & (Join-Path $PSScriptRoot 'tools\build_sss.ps1') -Assets $assets -Output (Join-Path $link 'sss-masks.bin') -Overrides $sssDestination
        }
        $previousPresets = Join-Path $rollbackPack 'presets'
        if (Test-Path -LiteralPath $previousPresets -PathType Container) {
            $presetDestination = Join-Path $link 'presets'
            New-Item -ItemType Directory -Path $presetDestination -Force | Out-Null
            foreach ($preset in Get-ChildItem -LiteralPath $previousPresets -File -Filter 'preset-*.glsl') {
                $destination = Join-Path $presetDestination $preset.Name
                if (-not (Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $preset.FullName -Destination $destination }
            }
        }
    }
    Copy-Item -LiteralPath $source -Destination $dll -Force
    $hash = (Get-FileHash -LiteralPath $source).Hash
    if ((Get-FileHash -LiteralPath $dll).Hash -ne $hash) { throw 'Installed DLL checksum mismatch.' }
    $metadataChanged = $true
    [ordered]@{ Owner = 'Aurora Shaders'; Source = $root; InstalledHash = $hash; PackMode = $packMode;
                PreviousDll = $previousDllName; PreviousPack = $previousPackName } |
        ConvertTo-Json | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    Set-Content -LiteralPath $configFile -Value $ClientDir -Encoding UTF8
} catch {
    $failure = $_
    if ($metadataChanged) {
        if ($null -ne $oldReceipt) { [IO.File]::WriteAllBytes($receiptPath, $oldReceipt) }
        elseif (Test-Path -LiteralPath $receiptPath) { Remove-Item -LiteralPath $receiptPath }
        if ($null -ne $oldConfig) { [IO.File]::WriteAllBytes($configFile, $oldConfig) }
        elseif (Test-Path -LiteralPath $configFile) { Remove-Item -LiteralPath $configFile }
    }
    if ($newPackCreated -and (Test-Path -LiteralPath $link)) {
        $newPack = Get-Item -LiteralPath $link -Force
        if ($newPack.Attributes -band [IO.FileAttributes]::ReparsePoint) { $newPack.Delete() }
        else { Move-Item -LiteralPath $link -Destination (Client-Child "shaderpack.aurora-failed.$stamp") }
    }
    if ($packMoved -and (Test-Path -LiteralPath $rollbackPack)) { Move-Item -LiteralPath $rollbackPack -Destination $link }
    if ($dllSaved) { Copy-Item -LiteralPath $rollbackDll -Destination $dll -Force }
    elseif (Test-Path -LiteralPath $dll) { Remove-Item -LiteralPath $dll }
    throw $failure
}
try {
    if ($dllSaved -and [IO.Path]::GetFileName($rollbackDll) -ne $previousDllName) { Remove-Item -LiteralPath $rollbackDll }
    if ($packMoved -and $isLink -and [IO.Path]::GetFileName($rollbackPack) -ne $previousPackName) {
        (Get-Item -LiteralPath $rollbackPack -Force).Delete()
    }
    Save-OldClientFiles (Join-Path $root ('backups\reinstalled-' + $stamp))
} catch {
    Write-Warning "Installed successfully; an obsolete backup could not be removed: $_"
}
Write-Host "Aurora Shaders clean-installed ($packMode). Game shaders regenerated; DLL checksum verified."
Write-Host 'Disable TAA in Hytale graphics settings.'
Write-Host 'Your menu hotkey (Home by default) opens the panel. Rebind it under General. Arrow keys and PgUp/PgDn edit; hold Alt for mouse editing.'
if ($packMode -eq 'Copy') { Write-Host "Edit shaders and settings in $link; the original package is not linked on this drive." }
