@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
pushd "%~dp0..\d2rhud"
rc /nologo /fo "%outputRoot%\localization.res" d2rhud.rc
popd
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\hud-localization-test.exe" "%~dp0hud-localization-test.cpp" "%~dp0..\d2rhud\plugin\HudLocalization.cpp" "%outputRoot%\localization.res" /link /LIBPATH:"%deps%\lib" imgui.lib advapi32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\hud-localization-test.exe" "%~dp0.." "%outputRoot%"
exit /b %errorlevel%
