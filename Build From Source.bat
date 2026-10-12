@echo off
title Aurora Shaders - Build from source and install
cd /d "%~dp0"
echo Builds opengl32.dll from the C source in hook\ with the Zig compiler (downloaded from
echo ziglang.org and checksum-verified), then installs it. Use this if you would rather not
echo run the prebuilt DLL.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Build
echo.
pause
