@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\floating-damage-toggle-test.exe" "%~dp0floating-damage-toggle-test.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\floating-damage-toggle-test.exe"
exit /b %errorlevel%
:usage
echo Usage: tests\run-floating-damage.cmd ^<output directory^>
exit /b 1
