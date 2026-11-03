@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static"
if not exist "%deps%\include\detours\detours.h" set "deps=%deps%\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\hud-hook-setup-test.exe" "%~dp0hud-hook-setup-test.cpp" /link /LIBPATH:"%deps%\lib" detours.lib /OPT:NOICF
if errorlevel 1 exit /b 1
"%outputRoot%\hud-hook-setup-test.exe" %2
exit /b %errorlevel%
