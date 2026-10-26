@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\quest-journal-client-test.exe" "%~dp0quest-journal-client-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\quest-journal-client-test.exe" %2
exit /b %errorlevel%
