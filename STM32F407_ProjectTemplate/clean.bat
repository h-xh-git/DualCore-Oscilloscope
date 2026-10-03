@echo off
REM ---------------------------------------------------------------------------
REM DualCore-Oscilloscope - remove Keil MDK build artifacts
REM Run this before packaging or archiving the source tree.
REM ---------------------------------------------------------------------------
cd /d "%~dp0"

if exist "project\MDK(V5)\Listings"        rd /Q /S "project\MDK(V5)\Listings"
if exist "project\MDK(V5)\Objects"         rd /Q /S "project\MDK(V5)\Objects"
if exist "project\MDK(V5)\project.uvgui.*" del /Q "project\MDK(V5)\project.uvgui.*"

echo Build artifacts removed.
