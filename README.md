
<a href="https://buymeacoffee.com/kristijan1001" target="_blank"> <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" width="160" alt="Buy Me a Coffee"> </a>


https://github.com/user-attachments/assets/9c0842a6-0442-40e9-8760-67f695989e0c

<img width="3840" height="2160" alt="HytaleClient_ARPBNL3it1" src="https://github.com/user-attachments/assets/b79537e3-1a60-4631-a084-619f9bd18946" />

<img width="3840" height="2160" alt="HytaleClient_XLE7vjtrBp" src="https://github.com/user-attachments/assets/1cf4d1f9-71e0-4597-a119-d9321ea4f554" />

<img width="3840" height="2160" alt="HytaleClient_yiDS4pwOJz" src="https://github.com/user-attachments/assets/3d899ec1-089d-4af7-8fac-14bec9981215" />

<img width="3840" height="2160" alt="HytaleClient_9uBVvnw0u2" src="https://github.com/user-attachments/assets/fa8cc289-cdb0-4e77-b53e-8d5332a578e1" />

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/e35d3088-7386-4fbb-8ac3-0dd1624d1a57" />

<img width="3840" height="2160" alt="HytaleClient_nKmVvN2vCb" src="https://github.com/user-attachments/assets/287daae9-44d0-4441-810f-5fac7b174c9a" />

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/6c50f4f3-098d-4eac-86d0-67ac4d0e60f9" />

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/e63f95ea-0f36-4845-ae8d-54bf9d7eab6c" />


# Aurora Shaders for Hytale

A shader pack for Hytale: soft sun shadows, volumetric clouds, light shafts through trees and
caves, reflective water, ray-traced bounce light and ambient occlusion, rain puddles, rainbows,
a rotating star sky, lens flare and a filmic tonemap - with an in-game options menu.

> **Not tested online, use at your own discretion.**

## Install

1. Download the latest release zip from the [Releases](../../releases) page and extract it
   somewhere permanent (the game loads the pack from this folder, so don't delete it).
2. Close Hytale and double-click **`Install.bat`**.
3. Start Hytale.

The installer offers the detected game, an executable picker that supports any drive,
or a pasted installation/executable path. It reads the game's shaders from your own `HytaleClient.exe`, and puts a small
`opengl32.dll` next to the game. Nothing in the game itself is modified.

Install and Update both remove the previous Aurora files and links before installing the new build.
Changed DLL hashes or stale receipts do not block cleanup. Recovery files are stored under the
Aurora package's `backups/` folder, outside the game directory. If the destination
drive cannot create a junction, the installer copies the pack instead. Copied installs preserve
existing setting values on upgrade; edit that installed copy for hot reload. Installation failures
restore the previous client files, and the installed DLL is checksum-verified.

- **After a Hytale update**, run `Update.bat`, or choose **Update shaders for a new game version** in the installer. It clean-installs and regenerates shaders from the new client.
- **To remove it**, double-click `Uninstall.bat`: Aurora's DLL, shaderpack link/copy, receipt, and old Aurora install debris are removed from the game folder. Junction targets are preserved; recovery files stay outside the game.

Requires Windows 10 or 11 and a GPU that runs Hytale. The pack is heavier than vanilla,
especially the RTX preset; see [Performance](#performance).

## In game

| Key | |
|---|---|
| **Home** | Opens / closes the options menu. |
| **Hold Alt** | Edit the small left-side panel; release to move and look normally. |
| **Up / Down** | Select an option, without Alt. |
| **Left / Right** | Change it (hold to slide numbers), without Alt. |
| **PgUp / PgDn** | Switch category tabs, without Alt. |
| **Alt + Mouse** | Click tabs and switches; drag sliders; scroll the current category. |
| **Escape** | Close the menu. |
| **Pause** | Shaders off / on, for quick comparisons. |

Changes apply instantly; default values and slider markers are yellow, including On/Off switches.
Click the search box while holding Alt to search by setting label or define; Backspace edits
the search text only. Tab, Enter and Space do not navigate or change the menu.
Changed values are mint, and the selected option's default is always shown in the footer. Settings are saved in
`shaderpack/include/AU_Settings.glsl`, which you can also edit by hand while the game runs (saving
the file reloads the shaders).

The compact left-side menu leaves the centre of the game visible and does not capture movement
or camera input unless Alt is held. Keyboard shortcuts remain available without Alt;
typing in search captures keys until Enter or an arrow key leaves the search field.
The vertically centred panel has category tabs, global search, per-category scrolling, and help for
the selected setting. **Reset selected** resets only that option, never the entire pack.
The ore sweep uses a continuous world-space band with a feathered highlight instead of
triangle-by-triangle screen-space brightening. Its strength, speed and band width are in
**Ores & materials**, separate from the ore's base glow and surrounding light.
Ore blink strength and speed control a coloured glow pulse; zero strength restores steady glow.
**Ore animation** selects blinking plus the soft glint, the original Hytale glint without
blinking, or entirely steady glow. None of these choices disables the base coloured glow.

## Menu options

### General

| Option | What it does |
|---|---|
| **Shaders** | Turns the whole pack on or off (same as the Pause key). |
| **Preset** | **Standard**, or **RTX**: adds bounce light and ray-traced ambient occlusion (see the RTX section), shadows that get softer the further they fall from what casts them, wet-looking reflective surfaces in rain, and more samples for shadows, light shafts and clouds. Costs noticeably more GPU time. |
| **Brightness** | Overall brightness of shaded areas and nights, like Minecraft's brightness slider: low is moody, high is bright. |
| **Cave lighting %** | The minimum light in places the sun and sky can't reach - caves, deep interiors. Lower makes caves darker. |

### Lighting

| Option | What it does |
|---|---|
| **Ambient occlusion** | How dark corners, crevices and the spots under objects get. |
| **Torch light** | Brightness of light from torches, lanterns, lava and other light blocks. |
| **Torch colour (warm to Hytale)** | 0 gives every light a classic warm orange glow; 1 uses Hytale's own colour for each light (blue crystals, green lights...). |
| **Emissive glow** | How strongly glowing blocks themselves shine - lava, flames, crystals. |
| **Block relief** | Gives block textures a slight 3D surface, so stone, bricks and wood catch the light per pixel. |
| **Relief strength** | How deep that relief looks. |
| **Foliage translucency** | Sunlight shining through leaves and grass when the sun is behind them. |
| **Sun highlights** | Soft sun reflections on surfaces and a sheen on leaves. |

### Shadows

| Option | What it does |
|---|---|
| **Shadow softness** | How blurry shadow edges are. |
| **Contact shadows** | Small shadows for grass, plants and props, which Hytale leaves out of its shadow map. |
| **Distant shadows** | Shadows for trees and hills beyond the range of the normal shadows, so the landscape doesn't go flat in the distance. |

### RTX

These only apply with the **RTX** preset.

| Option | What it does |
|---|---|
| **Bounce light** | Sunlight bouncing off surfaces: shade under a tree picks up green from the sunlit grass, a room fills with warm light from a sunlit floor, torch flames light the walls around them. |
| **Bounce rays** | How many rays per pixel the bounce light uses. More is smoother but slower; this is the biggest performance setting in the pack. |
| **Bounce reach** | How far (in blocks) bounced light travels. |
| **Ray-traced AO** | How much those rays also darken enclosed corners and the shade under overhangs. |

### Sky

| Option | What it does |
|---|---|
| **Stars** | **Off**, **Procedural** (sparse generated stars), or **Hytale** (Hytale's own star texture with the milky way, turning across the sky through the night). |
| **Star brightness** | Brightness of the stars. |
| **Star density** | Size of the star field on the sky: lower gives bigger, fewer stars. |
| **Night sky blue** | How deep blue the night sky stays down to the horizon; 1 is a grey-blue. |
| **Rainbows** | A rainbow opposite the sun after rain, in the morning or evening. |
| **Atmospheric fog** | Haze that softens the distance. |
| **Fog density** | How thick that haze is. |

### Clouds

| Option | What it does |
|---|---|
| **Volumetric clouds** | Replaces Hytale's flat cloud layers with 3D clouds that the sun lights from inside. |
| **Cloud amount** | How much of the sky is covered. |
| **Cloud speed** | How fast they drift. |
| **Cloud layers** | One or two cloud layers. |
| **Cloud shadows** | Clouds cast moving shadows on the ground. |

### Light shafts

| Option | What it does |
|---|---|
| **Light shafts** | Volumetric rays of sun and moon light in the air. |
| **Strength** | Overall strength of the rays. |
| **Forest / cave beams** | How much the beams falling through gaps - between leaves, through roofs and cave openings - stand out from the general haze. 0 makes them plain haze. |
| **Beam boost** | Extra brightness for those beams. |
| **Moonlight haze** | Strength of the moonlit haze at night. |

### Water

| Option | What it does |
|---|---|
| **Shader water** | The pack's water: moving waves, reflections of the sky and the world, depth colour, sun glints. Off gives Hytale's own water. |
| **Refraction** | How much the water surface bends the view of the riverbed. |
| **Shore foam** | Foam along shorelines. |
| **Underwater caustics** | Rippling light patterns on surfaces while you are under water. |
| **Rain puddles** | Reflective puddles on open ground while it rains, drying up after. |
| **Puddle strength** | How many puddles form and how reflective they are. |

### Post-processing

| Option | What it does |
|---|---|
| **Exposure** | Overall image brightness. |
| **Contrast** | Difference between dark and bright. |
| **Saturation** | Colour intensity. |
| **Vibrance** | Boosts dull colours more than already strong ones. |
| **Bloom** | Glow around bright light: the sun, lava, torches. |
| **Bloom strength** | How strong that glow is. |
| **Lens flare** | Camera lens flares from the sun and moon. |
| **Lens flare strength** | How strong the flares are. |
| **Sun glare** | A soft glow around the visible sun. |
| **Vignette** | Slightly darker screen edges. |
| **Temporal anti-aliasing** | Smooths edges and cleans up the fine grain in shadows, light shafts and bounce light by combining frames. Turning it off makes those grainy. |
| **Sharpening** | Sharpens the image (0 is off). |

## Performance

The biggest costs, roughly in order: the **RTX** preset (try fewer **Bounce rays** first),
**Volumetric clouds**, **Light shafts**, and **Shader water** reflections. The Standard preset
runs much lighter than RTX.

## Build it yourself

The release includes a prebuilt `opengl32.dll`. If you would rather not run a DLL you did not
build, double-click **`Build From Source.bat`** instead of `Install.bat`: it compiles the DLL
from the C source in `hook/` and installs that. The compiler (Zig) is downloaded once from
ziglang.org and checked against the checksum ziglang.org publishes; no Visual Studio needed.

From PowerShell: `.\build.ps1` builds `build\opengl32.dll`, `.\install.ps1 -Build` builds and
installs.

## How it works

Hytale compiles its OpenGL shaders from source embedded in `HytaleClient.exe` and has no shader
loader. `opengl32.dll` in the game folder is loaded instead of the Windows one; it forwards every
call to the real `opengl32.dll`, and when the game compiles a shader it recognises (by
`shaderpack/index.txt`) and the pack has a replacement for in `shaderpack/programs/`, it compiles
that instead. Edited shader files are recompiled live. The in-game menu is drawn by the same DLL.

Hytale's own shader sources are not redistributed: `tools/prepare_shaders.ps1` extracts them from
the installed game into `shaderpack/original/` during install, because the pack's shaders include
parts of them.

Screen-space GI and path tracing use **Original** by default. It filters lighting in the
existing lighting pass at native resolution. **NVIDIA DLSS Ray Reconstruction** is the second
backend, selected in the GI denoising tab; it requires DLSS and a supported RTX GPU.
Original remains active until the NVIDIA backend is ready. Successful Ray Reconstruction
bypasses Original's spatial/temporal filtering and releases its history buffers.

Original exposes 1/4/9 history samples, spatial strength and normal tolerance, surface
matching tolerance and reach, maximum history weight, lighting-change rejection, and separate
PT/non-PT history lengths and enable switches. Spatial sharing and temporal history can be
disabled independently. Spatial-only filtering needs no history or current-frame lighting buffers.
Temporal history uses four textures (about 169 MiB at 1440p or 380 MiB at 4K). Local lighting
shares this history without extra denoiser buffers. Original culls local lights into a 64-cell
spatial bitmask in the unused light-list row. This 2,048-pixel preparation draw avoids sparse
whole-scene light sampling without another full-screen pass, extra textures, or extra shadow rays.
NVIDIA Ray Reconstruction retains its independent sampling path. Lite and Full and their extra filtering passes
have been removed. The block textures themselves are never filtered by the denoiser.

History lookup uses Hytale's reprojection matrix. Held-item history stays in view space and
cannot mix with world surfaces. Camera cuts, resizing, and shader reloads invalidate history.

Native emissive metadata is generated from Hytale's item definitions, parent inheritance,
states, textures, and tint variants. This includes luminous blue moss without making green
moss glow. The generated `emitters.json` reports coverage and missing texture references.
Native flood-fill lighting continues to work in every preset. Placed terrain/model emitters and ore
crystals additionally supply texture-colored area lighting with traced soft shadows in both
presets and both GI modes. Ore source brightness defaults to 30% of a torch and is independent
of ore surface glow and bloom. Area-light intensity, reach, size, and sampling cost are configurable.
Lighting's **Coloured bulb / potion light** independently scales illumination from coloured
emitter pixels (default 1.75), without increasing ore lighting or bloom.
Emissive fragments combine texture pixels with Hytale's own glow tint. Multicoloured fixtures
retain separate RGB hue groups per block, with sub-block positions and local falloff
so neighbouring bulbs do not collapse into one white light. Transparent emitters captured after
deferred lighting contribute on the next frame. This does not discover arbitrary emitters in
unloaded or never-rendered chunks. A small world-space cache retains observed emitters when
they leave the screen, instead of rebuilding lighting from visible pixels every frame. Unobserved
entries expire after one second by default, fading during the final quarter-second. Lighting's
**Light cache duration (seconds)** adjusts this independently of FPS. Leaving the voxel window
or reloading invalidates cached entries. This is bounded retention, not discovery of every light in the world.
Accumulated local-light colour also corrects Hytale's broad warm/white fill close to emitters,
instead of simply adding coloured bloom over a white pool. Lighting's **Native fill colour correction**
controls this blend. The fill uses a deterministic sum at light centres, independent of random
shadow-ray selection. Direct illumination sums RGB energy before sampling visibility separately
per channel; channels selecting the same source share a shadow ray (one to three rays total).
An unobstructed mixture is deterministic rather than randomly switching between bulb colours.
Only shadow visibility samples the area source. RR receives
the coloured lighting in HDR, while diffuse/specular material guides remain independent of light colour.
RR's noisy indirect lighting is combined in linear energy before the gamma-space game passes,
avoiding gamma amplification of rare samples into white bloom hotspots. Original retains its
existing post-accumulation lighting response.

Local lights share the geometry grid with PT. Without PT they allocate the geometry grid only
(32 MiB), skip the color grid and its screen-color feed pass, and denoise inside the existing
lighting pass. Area samples share their direction calculation across candidates, and out-of-range
sources are rejected before attenuation/shadow work.
RR uses the same spatial light culling as Original instead of sampling unrelated lights across
the entire voxel window. The culling draw covers only 32x64 pixels and reuses the inactive light-list
row; it adds no full-screen lighting pass or buffers.

DLSS receives HDR color before tonemapping and uses camera motion from Hytale's reprojection.
FXAA/pre-upscale sharpening are bypassed when DLSS is active, material mip bias follows the
actual render ratio, and a current-color bias mask covers emissives, foliage, models, and held
items. Tonemapping, grading, lens effects, and sharpening run on the reconstructed output.
Model selectors remain K/J for SR and E/D for RR; NVIDIA's default/driver overrides can select
a newer model. Independent animated-object motion vectors remain an engine integration limitation.

The first menu opening warns to disable **TAA in Hytale's Graphics settings**. This reminder
also stays in General. Aurora TAA/DLSS settings are separate. General's **Reset all categories**
resets shader-menu options only, with a second-click confirmation.

**Render resolution (live)** requests a native renderer resize while keeping the physical
window unchanged, then recreates the DLSS buffers at the resulting scene resolution. Leave
Hytale's Render Scale at 100% to avoid applying two scale factors. HUD viewports and clipping
remain mapped to the physical window.

**Subsurface scattering** defaults to voxel depth measurement for dense foliage and ice.
A bounded grid ray integrates occupied material length, coloured absorption and single-scattering
optical depth; empty gaps add no thickness and solid wood/stone blocks transmission. Measured
RGB transmission replaces the opaque foliage self-shadow term, and a shadow-map lookup beyond
the measured segment retains distant blockers. This follows
the volume-depth approach discussed in [issue #3](https://github.com/Kristijan1001/Hytale-Aurora-Shaders/issues/3#issuecomment-6183476310),
not shadow-map dithering. RTX feeds the result into its existing Original/Ray Reconstruction
lighting path. Thin grass retains surface translucency. The surface approximation remains selectable,
with volume density, absorption, reach and cell budget configurable in Lighting. The voxel method
reuses the geometry grid without extra full-screen passes, colour volumes or history textures;
when PT and area lights are off it still needs the 32 MiB geometry grid. Unseen geometry and
surfaces outside the grid retain the surface approximation, rather than claiming full-scene SSS.

**DLSS sharpness** is an edge-limited filter in the output resolve, before tonemapping. Zero skips its extra texture samples.
**Quality+ (77%)** and **Quality++ (85%)** are Aurora's custom render ratios using NVIDIA's
Quality mode; they are not additional NVIDIA model presets.

**DLSS Frame Generation** uses NVIDIA Streamline, Reflex and an input-transparent child
presentation surface. OpenGL keeps the game window; Vulkan presents generated frames on
the child surface to avoid mixing both swapchains on one window. SR/RR and Frame Generation
share one Vulkan device when used together, with shared inputs and GPU synchronization. It is off by default
and independent of Super Resolution. 2x/3x/4x/6x selections are clamped to GPU capability.
Normal OpenGL presentation is retained when disabled or unavailable. The backend's standalone
RTX 5080 test verified 112 actual NVIDIA presents for 56 rendered frames at 2x. In-game
validation remains necessary, especially HUD edges, transparent effects and moving models:
the current motion vectors include camera motion, but not independent object animation.
Install build dependencies and signed production runtimes with `tools\prepare_streamline.ps1`.
DLSS 5 Neural Rendering is also not implemented: NVIDIA's public renderer sample requires
the separate beta NGX SDK with `nvsdk_ngx_helpers_dlssnr_vk.h`, which is not in the SDK here.
The downloaded official Streamline 2.14.1 package has neither an NR plugin nor an NR header.
Ray Reconstruction is not relabelled as Neural Rendering.

## Troubleshooting

- **Looks like vanilla:** press Pause once (the shaders may be toggled off), and check that
  `opengl32.dll` sits next to `HytaleClient.exe`. After a game update, run `Install.bat` again.
- **Something renders wrong after a game update:** a shader that fails to compile falls back to
  the game's own; the compiler log is in `shaderpack/errors/`, and `shaderpack/hook.log` records
  what the DLL did. Please open an issue with those files.
- **Antivirus warning:** some antivirus tools flag DLLs that load next to a game. Use
  `Build From Source.bat` to build the DLL from the source yourself.

## Known Issue

- **When Sunrise hits for few seconds there is flicker on the opposite side from where the sun is.**

## Credits and license

Aurora Shaders is a modified pack based on Complementary Shaders by Complementary Development
(https://www.complementary.dev/). It is distributed under the Complementary License Agreement,
included unchanged in [LICENSE.txt](LICENSE.txt).

Shaders have been assisted by AI/Claude.
