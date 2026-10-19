# DLSS black-screen investigation

## Root cause (found after 1.4.3)

The black world was not DLSS. Every settings change hot-reloads the shaders, and the reload swapped freshly compiled shader objects into the game's programs. The game flags its shader objects for deletion right after attaching them, so detaching them freed them, and the driver gave their names to other passes' shaders. When a world loads, the game links its programs again from the shader objects it kept, without sending any source, and got the wrong shaders: the world rendered black while the UI stayed. Turning DLSS on from the menu is a settings change, which is why it looked like DLSS. It was reproduced on an RTX 5080 by changing two settings in the main menu and then loading a world. The hook log showed the same `OVERRIDE REJECTED ... attribute optimised away` lines at world load as the reporters' logs.

The reload now compiles the new sources into the game's own shader objects, which stay attached (`relink_program` in `hook/hook.c`). `tools/denoiser_test/reload.h` reproduces the game's pattern and fails on the 1.4.3 hook. As a safety net, the hook also compares the frame DLSS was given with what reached the window a few frames after DLSS starts or the pack reloads. If a lit frame arrives black twice, it logs the samples below and switches DLSS off for the session.

## Reports reviewed

- [Issue 12](https://github.com/Kristijan1001/Hytale-Aurora-Shaders/issues/12), including the original logs and follow-up reports: RTX 4060, Windows 10. Both Super Resolution and Ray Reconstruction create successfully, but the world is black while the UI remains visible. No NGX evaluation failure is logged. Successful creation alone does not establish that the captured or displayed pixels are valid.
- [Issue 14](https://github.com/Kristijan1001/Hytale-Aurora-Shaders/issues/14), including its comment and recording: RTX 5060 Ti, Windows 11. The recording shows the world becoming black with the UI remaining visible during the DLAA test. There is no separate log for its Frame Generation report.
- Issue 12's later Frame Generation capability error `4` is Streamline's `eErrorOSDisabledHWS`: hardware-accelerated GPU scheduling is disabled. This is separate from its earlier SR/RR black screen.

## Fixes and evidence

- Capture previously compared framebuffer coordinates against texture coordinates scaled by `uPixelSize`. A flipped fullscreen texture, offset viewport, or different source pixel size could reject almost every image write. GPU regression tests reproduced zero captured pixels in multiple cases. Capture now addresses the shared image directly from the capture viewport.
- Resolve now uses its output viewport rather than the game's potentially flipped texture coordinates. It also overrides and restores colour masks and rasterizer discard. Diagnostic input views use input dimensions instead of output dimensions.
- Newly created Vulkan images are explicitly released to OpenGL through the shared semaphore before their first use. Resizing waits for both APIs before freeing shared storage. Import failures, incorrect texture dimensions and Vulkan command failures are now checked rather than silently continuing.
- The capture/resolve regression uses the relevant blocks from the real post-effect shader and the actual resolve helper. The bridge fixture tests real SR/RR HDR output separately. Neither substitutes for confirmation on the reporting PCs.

The original RTX 4060 reporter [confirmed that DLSS works with the test build](https://github.com/Kristijan1001/Hytale-Aurora-Shaders/issues/12#issuecomment-6240971905). Version 1.4.3 includes those same fixes by default. Another reporter still describes a startup-specific black screen; use the diagnostic below if that persists. No model incompatibility is established by these reports.

## One-shot diagnostic

With DLSS enabled and the affected world visible, create an empty file named `_dlss_diagnose` in `shaderpack/programs`. The hook consumes it on reload and logs a single capture after three DLSS frames.

Send `shaderpack/hook.log`. Lines starting `dlss diagnostic:` contain capture viewport/pixel size, GL errors, and five pixel samples each from input colour, depth, motion and DLSS output. This distinguishes missing inputs from invalid network output or presentation. No screenshots or full-frame readbacks are saved. Normal rendering does not perform these diagnostic readbacks.

## Local checks

```powershell
& tools/dlss_test/presentation.ps1 -Run
& tools/dlss_test/build.ps1
& tools/dlss_test/dlss_test.exe
& tools/shader_test/build.ps1 -Run
```
