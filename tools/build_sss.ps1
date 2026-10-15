param([Parameter(Mandatory)][string]$Assets, [Parameter(Mandatory)][string]$Output, [Parameter(Mandatory)][string]$Overrides)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Assets $Assets -Output $Output -Overrides $Overrides
    if ($LASTEXITCODE -ne 0) { throw 'Foliage SSS mask generation failed.' }
    return
}
Add-Type -Path (Join-Path $PSScriptRoot 'sss_masks.cs') -ReferencedAssemblies System.Drawing, System.IO.Compression, System.IO.Compression.FileSystem
$count = [AuroraSSSMasks]::Build($Assets, $Output, $Overrides)
Write-Host "Foliage SSS masks: $count tiles"
