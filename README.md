# Aurora Shaders for Hytale

A shader pack for Hytale: soft sun shadows, volumetric clouds, light shafts through trees and
caves, reflective water, ray-traced bounce light and ambient occlusion, rain puddles, rainbows,
a rotating star sky, lens flare and a filmic tonemap - with an in-game options menu.

<!-- Screenshots -->

## Install

1. Download the latest release zip from the [Releases](../../releases) page and extract it
   somewhere permanent (the game loads the pack from this folder).
2. Close Hytale and double-click **`Install.bat`**.
3. Start Hytale.

The installer finds the game on its own (it asks for the folder if Hytale is installed
somewhere unusual), reads the game's shaders from your own `HytaleClient.exe`, and puts a small
`opengl32.dll` next to the game. Nothing in the game is modified: `Uninstall.bat` removes the
DLL and the game is back to vanilla.

**After a Hytale update**, run `Install.bat` again: updates replace the game folder.

## In game

| Key | |
|---|---|
| **Home** | Options menu. Arrows select and change, PgUp/PgDn jump between sections, Backspace restores the default. Changes apply instantly. |
| **Pause** | Shaders off / on, for comparisons. |

Two presets: **Standard**, and **RTX** (adds screen-space bounce light, ray-traced AO, wet
reflections and contact-hardening shadows). Every option is also in
`shaderpack/include/AU_Settings.glsl`; saving that file reloads the shaders in the running game.

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

- A shader that fails to compile falls back to the game's own; the compiler log is in
  `shaderpack/errors/`, and `shaderpack/hook.log` records what the DLL did.
- Some antivirus tools flag DLLs that load next to a game. Use `Build From Source.bat` to build
  the DLL from the source yourself.

## Credits and license

Aurora Shaders is a modified pack based on Complementary Shaders by Complementary Development
(https://www.complementary.dev/). It is distributed under the Complementary License Agreement,
included unchanged in [LICENSE.txt](LICENSE.txt).
