@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
set "casc=%~dp0..\..\D2RNexus\tools\CascLib\source\CascLib-38a34665624b8775bb875274b36191b21c38d97b\src"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\inspect-assets-test.exe" "%~dp0inspect-assets-test.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\HudConfigStore.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\inspect-assets-test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /I"%casc%" /Fo"%~f1\\" /Fe"%~f1\game-assets-packed-test.exe" "%~dp0game-assets-packed-test.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" /link /LIBPATH:"%TEMP%\d2rhud-buff-deps\x64-windows-static\lib" casc.lib zlib.lib
exit /b %errorlevel%
