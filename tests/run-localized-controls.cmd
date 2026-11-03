@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\localized-controls-test.exe" "%~dp0localized-controls-test.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib
if errorlevel 1 exit /b 1
"%outputRoot%\localized-controls-test.exe" "%~dp0.."
exit /b %errorlevel%
