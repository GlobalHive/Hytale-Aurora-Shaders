<img width="3840" height="2160" alt="HytaleClient_nKmVvN2vCb" src="https://github.com/user-attachments/assets/287daae9-44d0-4441-810f-5fac7b174c9a" />

<img width="3840" height="2160" alt="image" src="https://github.com/user-attachments/assets/6c50f4f3-098d-4eac-86d0-67ac4d0e60f9" />


# Aurora Shaders for Hytale

A shader pack for Hytale: soft sun shadows, volumetric clouds, light shafts through trees and
caves, reflective water, ray-traced bounce light and ambient occlusion, rain puddles, rainbows,
a rotating star sky, lens flare and a filmic tonemap - with an in-game options menu.

<!-- Screenshots -->

## Install

1. Download the latest release zip from the [Releases](../../releases) page and extract it
   somewhere permanent (the game loads the pack from this folder, so don't delete it).
2. Close Hytale and double-click **`Install.bat`**.
3. Start Hytale.

The installer finds the game on its own (it asks for the folder if Hytale is installed
somewhere unusual), reads the game's shaders from your own `HytaleClient.exe`, and puts a small
`opengl32.dll` next to the game. Nothing in the game itself is modified.

- **After a Hytale update**, run `Install.bat` again: updates replace the game folder.
- **To remove it**, double-click `Uninstall.bat`: the game is back to vanilla.

Requires Windows 10 or 11 and a GPU that runs Hytale. The pack is heavier than vanilla,
especially the RTX preset; see [Performance](#performance).

## In game

| Key | |
|---|---|
| **Home** | Opens / closes the options menu. |
| **Up / Down** | Select an option. |
| **Left / Right** | Change it (hold to slide numbers). |
| **PgUp / PgDn** | Jump to the previous / next section. |
| **Backspace** | Put the selected option back to its default. |
| **Pause** | Shaders off / on, for quick comparisons. |

Changes apply instantly; values you changed from the default show in orange. They are saved in
`shaderpack/include/AU_Settings.glsl`, which you can also edit by hand while the game runs (saving
the file reloads the shaders).

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

## Troubleshooting

- **Looks like vanilla:** press Pause once (the shaders may be toggled off), and check that
  `opengl32.dll` sits next to `HytaleClient.exe`. After a game update, run `Install.bat` again.
- **Something renders wrong after a game update:** a shader that fails to compile falls back to
  the game's own; the compiler log is in `shaderpack/errors/`, and `shaderpack/hook.log` records
  what the DLL did. Please open an issue with those files.
- **Antivirus warning:** some antivirus tools flag DLLs that load next to a game. Use
  `Build From Source.bat` to build the DLL from the source yourself.

## Credits and license

Aurora Shaders is a modified pack based on Complementary Shaders by Complementary Development
(https://www.complementary.dev/). It is distributed under the Complementary License Agreement,
included unchanged in [LICENSE.txt](LICENSE.txt).

Shaders have been assisted by AI/Claude.
