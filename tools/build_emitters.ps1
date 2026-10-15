param([Parameter(Mandatory)][string]$Assets, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -eq 'Core') {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Assets $Assets -Output $Output
    if ($LASTEXITCODE -ne 0) { throw 'Native emitter generation failed.' }
    return
}
Add-Type -AssemblyName System.IO.Compression.FileSystem, System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
public static class AuroraEmitterPixels {
    public static string[] Tiles(byte[] bytes) {
        var result = new List<string>();
        using (var stream = new MemoryStream(bytes)) using (var image = new Bitmap(stream)) {
            if (image.Width % 32 != 0 || image.Height % 32 != 0) return result.ToArray();
            for (int top = 0; top < image.Height; top += 32) for (int left = 0; left < image.Width; left += 32) {
                uint hash = 2166136261u;
                double red = 0, green = 0, blue = 0, weight = 0;
                for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
                    Color color = image.GetPixel(left + x, top + y);
                    byte alpha = color.A;
                    byte r = alpha == 0 ? (byte)0 : color.R, g = alpha == 0 ? (byte)0 : color.G, b = alpha == 0 ? (byte)0 : color.B;
                    unchecked { hash = (hash ^ r) * 16777619u; hash = (hash ^ g) * 16777619u; hash = (hash ^ b) * 16777619u; hash = (hash ^ alpha) * 16777619u; }
                    red += r * alpha; green += g * alpha; blue += b * alpha; weight += alpha;
                }
                if (weight > 0) result.Add(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                    "{0:x8} {1:x2}{2:x2}{3:x2}", hash, (int)(red / weight), (int)(green / weight), (int)(blue / weight)));
            }
        }
        return result.ToArray();
    }
}
'@ -ReferencedAssemblies System.Drawing

function Merge-Object($Base, $Overlay) {
    $result = @{}
    if ($Base) { foreach ($property in $Base.PSObject.Properties) { $result[$property.Name] = $property.Value } }
    if ($Overlay) {
        foreach ($property in $Overlay.PSObject.Properties) {
            if ($property.Value -is [pscustomobject] -and $result[$property.Name] -is [pscustomobject]) {
                $result[$property.Name] = Merge-Object $result[$property.Name] $property.Value
            } else { $result[$property.Name] = $property.Value }
        }
    }
    return [pscustomobject]$result
}
function Resolve-Item([string]$Name, [string[]]$Chain = @()) {
    if ($resolved.ContainsKey($Name)) { return $resolved[$Name] }
    if ($Chain -contains $Name -or $Chain.Count -gt 64 -or -not $definitions.ContainsKey($Name)) { return $null }
    $item = $definitions[$Name]
    $parent = if ($item.Parent) { Resolve-Item $item.Parent ($Chain + $Name) } else { $null }
    $resolved[$Name] = Merge-Object $parent $item
    return $resolved[$Name]
}
function Hex-Color([string]$Value) {
    $value = $Value.TrimStart('#')
    if ($value.Length -eq 3) { $value = -join ($value.ToCharArray() | ForEach-Object { "$_$_" }) }
    if ($value -notmatch '^[0-9a-fA-F]{6}$') { return $null }
    return $value.ToLowerInvariant()
}
$definitions = @{}
$resolved = @{}
$pixels = @{}
$lines = New-Object 'System.Collections.Generic.HashSet[string]'
$report = New-Object System.Collections.Generic.List[object]
$zip = [IO.Compression.ZipFile]::OpenRead($Assets)
try {
    $entries = @{}
    foreach ($entry in $zip.Entries) {
        $entries[$entry.FullName] = $entry
        if ($entry.FullName -notmatch '^Server/Item/Items/.*\.json$') { continue }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $definitions[[IO.Path]::GetFileNameWithoutExtension($entry.FullName)] = $reader.ReadToEnd() | ConvertFrom-Json }
        finally { $reader.Dispose() }
    }
    foreach ($name in @($definitions.Keys) | Sort-Object) {
        $item = Resolve-Item $name
        if (-not $item.BlockType) { continue }
        $blocks = @($item.BlockType)
        if ($item.BlockType.State.Definitions) {
            foreach ($state in $item.BlockType.State.Definitions.PSObject.Properties) {
                $block = Merge-Object $item.BlockType $state.Value
                if ($state.Value.DrawType -and $state.Value.DrawType -ne $item.BlockType.DrawType -and
                    -not $state.Value.PSObject.Properties['Light']) { $block.PSObject.Properties.Remove('Light') }
                $blocks += $block
            }
        }
        foreach ($block in $blocks) {
            if (-not $block.Light.Color) { continue }
            $color = Hex-Color $block.Light.Color
            if (-not $color -or $color -eq '000000') { continue }
            $textures = New-Object 'System.Collections.Generic.HashSet[string]'
            $textureSets = if ($block.DrawType -eq 'Model') { @() } else { @($block.Textures) }
            foreach ($textureSet in $textureSets) {
                if (-not $textureSet) { continue }
                foreach ($property in $textureSet.PSObject.Properties) {
                    if ($property.Value -is [string] -and $property.Value -match '\.png$') { [void]$textures.Add('Common/' + $property.Value) }
                }
            }
            $modelTextures = if ($block.DrawType -eq 'Model') { @($block.CustomModelTexture) } else { @() }
            foreach ($texture in $modelTextures) {
                if ($texture.Texture) { [void]$textures.Add('Common/' + $texture.Texture) }
            }
            $tints = @($block.Tint | ForEach-Object { Hex-Color $_ } | Where-Object { $_ })
            if (-not $tints.Count) { $tints = @('ffffffff') }
            $matched = 0
            foreach ($texture in $textures) {
                if (-not $entries.ContainsKey($texture)) { continue }
                if (-not $pixels.ContainsKey($texture)) {
                    $stream = $entries[$texture].Open()
                    $memory = New-Object IO.MemoryStream
                    try { $stream.CopyTo($memory); $pixels[$texture] = [AuroraEmitterPixels]::Tiles($memory.ToArray()) }
                    finally { $stream.Dispose(); $memory.Dispose() }
                }
                foreach ($tile in $pixels[$texture]) {
                    $parts = $tile.Split(' ')
                    foreach ($tint in $tints) { [void]$lines.Add("$($parts[0]) $tint $color $($parts[1])") }
                    $matched++
                }
            }
            $report.Add([pscustomobject]@{ Item = $name; Color = $color; Tint = $tints; Textures = @($textures); Tiles = $matched })
        }
    }
} finally { $zip.Dispose() }
[IO.File]::WriteAllLines($Output, [string[]]@($lines | Sort-Object), (New-Object Text.UTF8Encoding($false)))
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ([IO.Path]::ChangeExtension($Output, '.json')) -Encoding UTF8
Write-Host "Native emissives: $($report.Count) item/state definitions, $($lines.Count) texture/tint records"
