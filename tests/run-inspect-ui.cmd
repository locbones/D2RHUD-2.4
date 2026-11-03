@echo off
setlocal
if "%~1"=="" exit /b 1
if "%~2"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
if not defined D2RHUD_CASC_INCLUDE set "D2RHUD_CASC_INCLUDE=%~dp0..\..\D2RNexus\tools\CascLib\source\CascLib-38a34665624b8775bb875274b36191b21c38d97b\src"
if not defined D2RHUD_CASC_LIB set "D2RHUD_CASC_LIB=%TEMP%\d2rhud-buff-deps\x64-windows-static\lib"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /I"%D2RHUD_CASC_INCLUDE%" /Fo"%~f1\\" /Fe"%~f1\inspect-ui-test.exe" "%~dp0inspect-ui-test.cpp" "%~dp0..\d2rhud\plugin\InspectAssets.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\HudConfigStore.cpp" /link /LIBPATH:"%deps%\lib" /LIBPATH:"%D2RHUD_CASC_LIB%" imgui.lib user32.lib casc.lib zlib.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\inspect-ui-test.exe" "%~f1" "%~f2"
exit /b %errorlevel%
