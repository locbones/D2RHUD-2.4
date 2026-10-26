@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\options-layout-test.exe" "%~dp0options-layout-test.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\options-layout-test.exe"
exit /b %errorlevel%
