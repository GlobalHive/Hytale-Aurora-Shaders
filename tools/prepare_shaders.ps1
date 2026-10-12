# Prepares shaderpack/ for the installed game version:
#   1. extracts Hytale's GLSL from HytaleClient.exe into shaderpack/original (the pack's overrides
#      include parts of it, and it cannot be redistributed, so every install pulls it from the
#      player's own game),
#   2. builds shaderpack/index.txt (unique source lines the hook uses to recognise each shader),
#   3. generates shaderpack/programs/MapChunkFS.glsl from the game's MapChunkFS + tools/mapchunk/*.
#
#   prepare_shaders.ps1 -Exe "<...>\Client\HytaleClient.exe"
#   prepare_shaders.ps1 -MapChunkOnly        (after editing tools/mapchunk/*.glsl)
param(
    [string]$Exe,
    [switch]$MapChunkOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$pack = Join-Path $root 'shaderpack'

Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

public static class AuroraPrep
{
    static readonly UTF8Encoding Utf8 = new UTF8Encoding(false);

    // NativeAOT "NativeFormat" unsigned integer.
    static int Nu(byte[] d, ref int p)
    {
        int b0 = d[p];
        if ((b0 & 1) == 0) { p += 1; return b0 >> 1; }
        if ((b0 & 2) == 0) { int v = (b0 >> 2) | (d[p + 1] << 6); p += 2; return v; }
        if ((b0 & 4) == 0) { int v = (b0 >> 3) | (d[p + 1] << 5) | (d[p + 2] << 13); p += 3; return v; }
        if ((b0 & 8) == 0) { int v = (b0 >> 4) | (d[p + 1] << 4) | (d[p + 2] << 12) | (d[p + 3] << 20); p += 4; return v; }
        int r = BitConverter.ToInt32(d, p + 1); p += 5; return r;
    }

    static List<int> FindAll(byte[] d, byte[] pat)
    {
        var hits = new List<int>();
        byte first = pat[0];
        for (int i = Array.IndexOf(d, first); i >= 0 && i <= d.Length - pat.Length; i = Array.IndexOf(d, first, i + 1))
        {
            int k = 1;
            while (k < pat.Length && d[i + k] == pat[k]) k++;
            if (k == pat.Length) hits.Add(i);
        }
        return hits;
    }

    static bool StartsWithAt(byte[] d, int at, byte[] pat)
    {
        if (at < 0 || at + pat.Length > d.Length) return false;
        for (int k = 0; k < pat.Length; k++) if (d[at + k] != pat[k]) return false;
        return true;
    }

    // Returns the number of shader files written.
    public static int Extract(string exe, string outDir)
    {
        byte[] d = File.ReadAllBytes(exe);
        byte[] asm = Encoding.ASCII.GetBytes("HytaleClient, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null");
        const string prefix = "HytaleClient.Graphics.Shaders.";
        var res = new List<Tuple<string, int, int>>();
        foreach (int m in FindAll(d, asm))
        {
            int p = m + asm.Length;
            int n = Nu(d, ref p);
            if (n <= 0 || n > 512 || p + n > d.Length) continue;
            string name = Encoding.ASCII.GetString(d, p, n);
            if (!name.StartsWith(prefix)) continue;
            int q = p + n;
            int off = Nu(d, ref q);
            int len = Nu(d, ref q);
            res.Add(Tuple.Create(name.Substring(prefix.Length), off, len));
        }
        if (res.Count == 0) throw new Exception("No shader resources found in " + exe);

        // The resources are stored back to back in one blob; find its start by checking which
        // "#version" occurrence makes the most vertex/fragment shaders start with "#version".
        byte[] ver = Encoding.ASCII.GetBytes("#version 410 core");
        var mains = res.Where(r => r.Item1.EndsWith("FS.glsl") || r.Item1.EndsWith("VS.glsl")).ToList();
        var first = mains.OrderBy(r => r.Item2).First();
        int bestBase = -1, bestScore = 0;
        foreach (int occ in FindAll(d, ver))
        {
            int b = occ - first.Item2;
            int score = mains.Count(r => StartsWithAt(d, b + r.Item2, ver));
            if (score > bestScore) { bestScore = score; bestBase = b; }
        }
        if (bestBase < 0 || bestScore < mains.Count / 2) throw new Exception("Could not locate the shader data in " + exe);

        Directory.CreateDirectory(outDir);
        foreach (var r in res)
        {
            if (bestBase + r.Item2 + r.Item3 > d.Length) continue;
            var data = new byte[r.Item3];
            Array.Copy(d, bestBase + r.Item2, data, 0, r.Item3);
            File.WriteAllBytes(Path.Combine(outDir, r.Item1), data);
        }
        return res.Count;
    }

    // For every main (non-include) shader: up to three source lines found in no other shader.
    // The hook hashes each line of a runtime shader and identifies it by these.
    public static int BuildIndex(string origDir, string outPath)
    {
        var files = Directory.GetFiles(origDir, "*.glsl").OrderBy(f => Path.GetFileName(f), StringComparer.Ordinal).ToList();
        var text = files.ToDictionary(f => Path.GetFileName(f), f => File.ReadAllText(f, Utf8));
        var included = new HashSet<string>();
        foreach (var t in text.Values)
            foreach (Match m in Regex.Matches(t, "#include\\s*\"([^\"]+)\"")) included.Add(m.Groups[1].Value);
        var lines = text.ToDictionary(kv => kv.Key, kv => new HashSet<string>(
            kv.Value.Split('\n').Select(l => l.Trim()).Where(l => l.Length > 0)));
        var count = new Dictionary<string, int>();
        foreach (var set in lines.Values)
            foreach (var l in set) { int c; count[l] = count.TryGetValue(l, out c) ? c + 1 : 1; }
        var outLines = new List<string>();
        foreach (var name in text.Keys.Where(n => !included.Contains(n)))
        {
            var sigs = lines[name]
                .Where(l => count[l] == 1 && l.Length >= 20 && !l.Contains('\t')
                            && !l.StartsWith("#include") && !l.StartsWith("#version"))
                .OrderBy(l => l.StartsWith("//") ? 1 : 0).ThenByDescending(l => l.Length).ThenBy(l => l, StringComparer.Ordinal)
                .Take(3).ToList();
            if (sigs.Count == 0) continue;
            outLines.Add(name.Substring(0, name.Length - 5) + "\t" + string.Join("\t", sigs));
        }
        File.WriteAllText(outPath, string.Join("\n", outLines) + "\n", Utf8);
        return outLines.Count;
    }

    static string Read(string path)
    {
        return File.ReadAllText(path, Utf8).TrimStart((char)0xFEFF).Replace("\r\n", "\n");
    }

    static string Rep(string s, string a, string b)
    {
        int i = s.IndexOf(a, StringComparison.Ordinal);
        if (i < 0) throw new Exception("MapChunkFS: game shader changed, anchor not found: " + a.Substring(0, Math.Min(70, a.Length)).Replace("\n", "\\n"));
        return s.Substring(0, i) + b + s.Substring(i + a.Length);
    }

    // The game's MapChunkFS with the pack's G-buffer normals, forward lighting, water and fog
    // spliced in.
    public static void MakeMapChunk(string pack, string snip)
    {
        string s = Read(Path.Combine(pack, "original", "MapChunkFS.glsl"));
        s = Rep(s, "#endif // !DEFERRED\n\n#if USE_OVERLAY",
                "#endif // !DEFERRED\n\n" + Read(Path.Combine(snip, "includes.glsl")) + "\n#if USE_OVERLAY");
        // Hytale sweeps a fake shine over REFLECTIVE blocks in near chunks only, boosting their
        // static light; the pack's lighting reads that as block light and the near chunks glow
        // in a square around the player. The pack has its own highlights.
        s = Rep(s, "    if (shadingMode == SHADING_REFLECTIVE)\n    {\n"
                 + "        vec4 shininess = computeFakeShininess(texColor.rgb, normalWS, CameraDirection.xyz);\n"
                 + "        light = max(light, vec3(shininess.a));\n        albedo.rgb += shininess.rgb;\n    }\n",
                "    // (Hytale fake shininess removed by Aurora Shaders)\n");
        s = Rep(s, "    bool useCleanShadowBackfaces = effectID > EFFECT_WIND;\n\n#if DEFERRED\n\n    writeToGBuffer(",
                "    bool useCleanShadowBackfaces = effectID > EFFECT_WIND;\n\n#if DEFERRED\n"
                + Read(Path.Combine(snip, "gennormals.glsl")) + "\n    writeToGBuffer(");
        string lit = "    outColor0.rgb = albedo * light * shadow;\n    outColor0.a = texColor.a;\n";
        s = Rep(s, lit, lit + "\n" + Read(Path.Combine(snip, "lighting.glsl")));

        int start = s.IndexOf("    if (isWater(effectID) && uWaterQuality >= 1)\n    {\n        vec2 screenUV", StringComparison.Ordinal);
        int end = s.IndexOf("    // Warning : because of precision issue \"with the UVs\",", StringComparison.Ordinal);
        if (start < 0 || end < start) throw new Exception("MapChunkFS: game shader changed, water block not found");
        string origWater = s.Substring(start, end - start);
        int bodyStart = origWater.IndexOf("{\n", StringComparison.Ordinal) + 2;
        string origBody = origWater.Substring(bodyStart, origWater.LastIndexOf('}') - bodyStart);
        s = s.Substring(0, start)
            + "    if (isWater(effectID) && uWaterQuality >= 1)\n    {\n#if AU_FORWARD && AU_WATER\n"
            + Read(Path.Combine(snip, "water.glsl")) + "#else\n" + origBody + "#endif\n    }\n\n"
            + s.Substring(end);

        s = Rep(s, "#if USE_FOG\n    // Apply fog\n    outColor0.rgb = mix(outColor0.rgb, fragFog.rgb, fragFog.w);\n"
                 + "    hasBloom = hasBloom && fragFog.w < 0.9;\n#endif // USE_FOG",
                "#if USE_FOG\n#if AU_FORWARD\n    DoAUFog(outColor0.rgb, fragPositionWS, auDither, fragStaticLight.a);\n"
                + "    outColor0.rgb = AU_AddVL(outColor0.rgb, auForwardVL);\n#else\n"
                + "    // Apply fog\n    outColor0.rgb = mix(outColor0.rgb, fragFog.rgb, fragFog.w);\n#endif\n"
                + "    hasBloom = hasBloom && fragFog.w < 0.9;\n#endif // USE_FOG");
        s = Rep(s, "#version 410 core\n",
                "#version 410 core\n// Aurora Shaders for Hytale - terrain (G-buffer) and forward water / translucents.\n"
                + "// Generated by tools/prepare_shaders.ps1 from the game shader + tools/mapchunk/*.glsl.\n");
        File.WriteAllText(Path.Combine(pack, "programs", "MapChunkFS.glsl"), s, Utf8);
    }

    // The game's MapChunkVS, recording every block it draws in the path tracer's voxel grid.
    public static void MakeMapChunkVS(string pack, string snip)
    {
        string s = Read(Path.Combine(pack, "original", "MapChunkVS.glsl"));
        s = Rep(s, "#version 410 core\n",
                "#version 410 core\n#extension GL_ARB_shader_image_load_store : enable\n"
                + "// Aurora Shaders for Hytale - terrain vertices + voxel grid.\n"
                + "// Generated by tools/prepare_shaders.ps1 from the game shader + tools/mapchunk/voxelize.glsl.\n");
        s = Rep(s, "#include \"Shading_inc.glsl\"\n",
                "#include \"Shading_inc.glsl\"\n#include \"AU_Settings.glsl\"\n"
                + "#if AU_VOXELS && DEFERRED && !ALPHA_BLEND && !ANIMATED && !USE_SCENE_DATA_OVERRIDE\n"
                + "#define AU_VOX_WRITER_ONLY\n#include \"AU_Voxel.glsl\"\n#endif\n");
        s = Rep(s, "    fragPackedData = packData(effectID, shadingMode);\n",
                Read(Path.Combine(snip, "voxelize.glsl")) + "\n    fragPackedData = packData(effectID, shadingMode);\n");
        File.WriteAllText(Path.Combine(pack, "programs", "MapChunkVS.glsl"), s, Utf8);
    }
}
'@

if (-not $MapChunkOnly) {
    if (-not $Exe) { throw 'Pass -Exe "<...>\Client\HytaleClient.exe".' }
    $orig = Join-Path $pack 'original'
    $n = [AuroraPrep]::Extract($Exe, $orig)
    Write-Host "Extracted $n game shaders into shaderpack\original"
    $n = [AuroraPrep]::BuildIndex($orig, (Join-Path $pack 'index.txt'))
    Write-Host "Shader index: $n entries"
}
[AuroraPrep]::MakeMapChunk($pack, (Join-Path $PSScriptRoot 'mapchunk'))
Write-Host 'Generated shaderpack\programs\MapChunkFS.glsl'
[AuroraPrep]::MakeMapChunkVS($pack, (Join-Path $PSScriptRoot 'mapchunk'))
Write-Host 'Generated shaderpack\programs\MapChunkVS.glsl'
