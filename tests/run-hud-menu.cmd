@echo off
setlocal
if "%~3"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\hud-menu-test.exe" "%~dp0hud-menu-test.cpp" /link /LIBPATH:"%deps%\lib" lua.lib imgui.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\hud-menu-test.exe" "%~f1" "%~f2" "%~f3" %4
exit /b %errorlevel%
