# Foliage SSS masks

In Lighting, choose **Scattering method**:
- **Voxel depth + accumulation** measures foliage/ice thickness. This stays the default.
- **Foliage shadow dithering** opens selected pixels in leaf shadows. Permeability 0 is opaque; 0.5 and 0.8 let more light through.
- **Surface approximation** keeps the simpler surface response.

Dithering affects sun-shadow depth only, not camera depth, grass, bark or solid blocks. Its pattern is attached to leaf UVs and block/face IDs, so it does not change randomly between frames. Voxel transmission and shadow dithering are separate methods, not stacked effects.

If a distant shadow cascade is cached, move a little after changing the mode or permeability so Hytale refreshes it.

The installer builds `sss-masks.bin` from your game's leaf textures. Dark veins transmit less than bright leaf tissue. These automatic masks are an approximation, not hand-painted material data. The masks use a small R8 texture array; they add no full-screen pass or denoiser history.

## Custom masks

Use a grayscale PNG with the same dimensions as the original leaf texture. Black is opaque; white is thin, transmitting tissue. Mirror its path under this folder, without the leading `Common/`:

`Common/Blocks/Foliage/Leaves/Ball_Textures/Oak.png` → `shaderpack/sss/Blocks/Foliage/Leaves/Ball_Textures/Oak.png`

Transparent source pixels stay transparent. Root and spider-web textures are excluded. Identical source tiles share one mask; conflicting custom masks for an identical tile are rejected.

Run the installer again to rebuild masks after editing. Alternatively, run `tools/build_sss.ps1` with `-Assets`, `-Output` and `-Overrides`, or `tools/build_sss.py` with the equivalent lowercase arguments. Point Output at `shaderpack/sss-masks.bin` and Overrides at this folder. The running hook reloads regenerated masks.

On Linux, edit the installed `Client/.aurora/shaderpack/sss/` folder. Custom masks are retained during updates on both platforms. **Foliage SSS textures** disables per-pixel masks; **SSS texture influence** blends between uniform and textured transmission.
