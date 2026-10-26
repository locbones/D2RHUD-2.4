@echo off
setlocal
if "%~4"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\settings-ui-test.exe" "%~dp0settings-ui-test.cpp" "%~dp0..\d2rhud\plugin\StatRangesData.cpp" "%~dp0..\d2rhud\plugin\TooltipText.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib detours.lib casc.lib zlib.lib user32.lib advapi32.lib wininet.lib
if errorlevel 1 exit /b 1
"%outputRoot%\settings-ui-test.exe" "%~f1" "%~f2" "%~f3" "%~f4"
exit /b %errorlevel%
