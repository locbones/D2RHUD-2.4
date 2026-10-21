@echo off
setlocal
if "%~2"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\cube-map-ui-test.exe" "%~dp0cube-map-ui-test.cpp" "%~dp0..\d2rhud\plugin\SummonPersistence.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib imgui.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\cube-map-ui-test.exe" "%outputRoot%" "%~2"
exit /b %errorlevel%
