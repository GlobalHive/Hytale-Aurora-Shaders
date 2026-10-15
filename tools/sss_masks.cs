using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

public static class AuroraSSSMasks
{
    public static int Build(string assets, string output, string overrides)
    {
        var records = new SortedDictionary<uint, byte[]>();
        var authored = new HashSet<uint>();
        using (var archive = ZipFile.OpenRead(assets))
        foreach (var entry in archive.Entries.OrderBy(value => value.FullName, StringComparer.Ordinal))
        {
            string name = entry.FullName;
            string stem = Path.GetFileNameWithoutExtension(name).ToLowerInvariant();
            if (!name.StartsWith("Common/Blocks/Foliage/Leaves/", StringComparison.Ordinal) || !name.EndsWith(".png", StringComparison.Ordinal)
                || stem.Contains("root") || stem.Contains("spider") || stem.Contains("web") || name.Split('/').Contains("..")) continue;
            using (var stream = entry.Open())
            using (var data = new MemoryStream())
            {
                stream.CopyTo(data);
                data.Position = 0;
                using (var image = new Bitmap(data))
                {
                    string overridePath = Path.Combine(overrides, name.Substring(7).Replace('/', Path.DirectorySeparatorChar));
                    using (var custom = File.Exists(overridePath) ? new Bitmap(overridePath) : null)
                    {
                        if (image.Width % 32 != 0 || image.Height % 32 != 0) throw new Exception("Foliage textures must contain complete 32x32 atlas tiles");
                        if (custom != null && (custom.Width != image.Width || custom.Height != image.Height))
                            throw new Exception("SSS override dimensions must match " + name);
                        for (int top = 0; top < image.Height; top += 32)
                        for (int left = 0; left < image.Width; left += 32)
                        {
                            var pixels = new Color[1024];
                            var luminance = new int[1024];
                            var visible = new List<int>();
                            for (int row = 0; row < 32; row++)
                            for (int column = 0; column < 32; column++)
                            {
                                int index = row * 32 + column;
                                Color pixel = image.GetPixel(left + column, top + row);
                                pixels[index] = pixel;
                                luminance[index] = (54 * pixel.R + 183 * pixel.G + 19 * pixel.B + 128) >> 8;
                                if (pixel.A != 0) visible.Add(luminance[index]);
                            }
                            if (visible.Count == 0) continue;
                            visible.Sort();
                            int lower = visible[(visible.Count - 1) / 10], upper = visible[(visible.Count - 1) * 9 / 10];
                            if (upper - lower < 16 && visible[visible.Count - 1] - visible[0] >= 16)
                            { lower = visible[0]; upper = visible[visible.Count - 1]; }
                            uint hash = 2166136261u;
                            var mask = new byte[1024];
                            for (int index = 0; index < 1024; index++)
                            {
                                Color pixel = pixels[index];
                                byte red = pixel.A == 0 ? (byte)0 : pixel.R;
                                byte green = pixel.A == 0 ? (byte)0 : pixel.G;
                                byte blue = pixel.A == 0 ? (byte)0 : pixel.B;
                                unchecked { hash = (hash ^ red) * 16777619u; hash = (hash ^ green) * 16777619u; hash = (hash ^ blue) * 16777619u; hash = (hash ^ pixel.A) * 16777619u; }
                                if (pixel.A == 0) continue;
                                if (custom != null)
                                {
                                    Color value = custom.GetPixel(left + index % 32, top + index / 32);
                                    mask[index] = (byte)(value.R * value.A / 255);
                                }
                                else if (upper - lower < 16) mask[index] = 192;
                                else
                                {
                                    double weight = Math.Min(1.0, Math.Max(0.0, (double)(luminance[index] - lower) / (upper - lower)));
                                    mask[index] = (byte)Math.Round(weight * weight * (3.0 - 2.0 * weight) * 255.0);
                                }
                            }
                            byte[] previous;
                            if (custom != null)
                            {
                                if (authored.Contains(hash) && !records[hash].SequenceEqual(mask))
                                    throw new Exception("Conflicting SSS overrides for identical atlas tile " + hash.ToString("x8"));
                                records[hash] = mask;
                                authored.Add(hash);
                            }
                            else if (!authored.Contains(hash))
                            {
                                if (records.TryGetValue(hash, out previous))
                                    for (int index = 0; index < mask.Length; index++) mask[index] = Math.Min(mask[index], previous[index]);
                                records[hash] = mask;
                            }
                        }
                    }
                }
            }
        }
        if (records.Count > 2047) throw new Exception("Too many distinct SSS mask tiles for the supported texture array");
        string directory = Path.GetDirectoryName(Path.GetFullPath(output));
        Directory.CreateDirectory(directory);
        string pending = Path.Combine(directory, ".sss-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            using (var writer = new BinaryWriter(File.Create(pending)))
            {
                writer.Write(Encoding.ASCII.GetBytes("AUSSS001"));
                writer.Write(records.Count);
                foreach (var record in records) { writer.Write(record.Key); writer.Write(record.Value); }
            }
            if (File.Exists(output)) File.Replace(pending, output, null);
            else File.Move(pending, output);
        }
        finally { if (File.Exists(pending)) File.Delete(pending); }
        return records.Count;
    }
}
