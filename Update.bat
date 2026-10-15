@echo off
title Aurora Shaders - Update for a new game version
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" -Update -ChooseLocation
echo.
pause
