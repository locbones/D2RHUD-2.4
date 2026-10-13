@echo off
setlocal
if "%~1"=="" goto usage
if "%~2"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 (
    echo Run from an x64 Native Tools Command Prompt for VS 2022.
    exit /b 1
)
set "dependencyRoot=%~f1"
set "outputRoot=%~f2"
set "sourceRoot=%~dp0.."
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1

cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\queue-test.exe" "%~dp0queue-test.cpp" "%sourceRoot%\d2rhud\WineQueue.cpp" /link /LIBPATH:"%dependencyRoot%\lib" detours.lib minhook.x64.lib
if errorlevel 1 exit /b 1
"%outputRoot%\queue-test.exe"
if errorlevel 1 exit /b 1

exit /b %errorlevel%

:usage
echo Usage: tests\run.cmd ^<x64-windows-static dependency directory^> ^<test output directory^>
exit /b 1
