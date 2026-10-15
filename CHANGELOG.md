# Changelog

## 1.3.0

- Added RTX and Non-RTX quality profiles, from Lowest to Ultra, plus a saved Default profile.
- Coloured area lights now enable only in Ultra quality profiles; manual controls remain available.
- Fixed Non-RTX area-light denoising and reduced Original denoiser grain.
- Improved light-cache turnover and culling so new cave lights load reliably with less overhead.
- Added per-pixel foliage transmission masks and optional shadow dithering.
- Added more quality controls and preserved custom foliage masks during Windows/Linux updates.

## 1.2.0

- RTX is now default; Standard is marked deprecated.
- Added Default, DLSS Preset and Path Tracing profiles, plus named custom presets.
- Restored configurable Original denoising; added DLSS Ray Reconstruction and fixed lighting-history trails.
- Improved DLSS HDR/motion handling, live resolution, Quality+/Quality++ and sharpness; fixed black speckles on bright lights.
- Added texture-coloured emissive lighting, stable bulb/potion colours, shorter light caching and torch-relative ore lighting.
- Added foliage/ice depth-based transmission and reduced lighting overhead.
- Redesigned the menu with tabs, reset controls and a native-TAA warning.
- Improved clean install/update/uninstall and added native x86_64 Linux installation.
- Included experimental Windows Frame Generation; off by default, with in-game validation still pending.
