@echo off
setlocal
if "%~4"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\candy-preview-replay-test.exe" "%~dp0candy-preview-replay-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\candy-preview-replay-test.exe" "%outputRoot%\data" "%~2" "%~3" "%~4"
exit /b %errorlevel%
