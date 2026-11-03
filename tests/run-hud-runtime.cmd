@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static"
if not exist "%deps%\include\sol\sol.hpp" set "deps=%deps%\x64-windows-static"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W3 /utf-8 /DNDEBUG /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\hud-runtime-test.exe" "%~dp0hud-runtime-test.cpp" /link /LIBPATH:"%deps%\lib" detours.lib imgui.lib user32.lib gdi32.lib imm32.lib dwmapi.lib
if errorlevel 1 exit /b 1
"%outputRoot%\hud-runtime-test.exe"
exit /b %errorlevel%
