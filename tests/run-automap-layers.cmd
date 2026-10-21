@echo off
setlocal
if "%~1"=="" goto usage
if "%~2"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "dependencyRoot=%~f1"
set "outputRoot=%~f2"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\automap-layers-test.exe" "%~dp0automap-layers-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" detours.lib
if errorlevel 1 exit /b 1
"%outputRoot%\automap-layers-test.exe" %3 %4
exit /b %errorlevel%
:usage
echo Usage: tests\run-automap-layers.cmd ^<x64-windows-static dependencies^> ^<output directory^> [D2R.exe path] [existing map directory]
exit /b 1
