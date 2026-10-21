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
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\sunder-test.exe" "%~dp0sunder-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" detours.lib
if errorlevel 1 exit /b 1
"%outputRoot%\sunder-test.exe" %3
exit /b %errorlevel%
:usage
echo Usage: tests\run-sunder.cmd ^<x64-windows-static dependencies^> ^<output directory^> [D2R.exe path]
exit /b 1
