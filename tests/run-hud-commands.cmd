@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static"
if not exist "%deps%\include\sol\sol.hpp" set "deps=%deps%\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\hud-commands-test.exe" "%~dp0hud-commands-test.cpp" "%~dp0..\d2rhud\plugin\KeyMappings.cpp" /link user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\hud-commands-test.exe"
exit /b %errorlevel%
