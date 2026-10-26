@echo off
setlocal
if "%~2"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\hellspindle-test.exe" "%~dp0hellspindle-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib
if errorlevel 1 exit /b 1
"%outputRoot%\hellspindle-test.exe" "%~2"
exit /b %errorlevel%
:usage
echo Usage: tests\run-hellspindle.cmd ^<output directory^> ^<audited D2R.exe^>
exit /b 1
