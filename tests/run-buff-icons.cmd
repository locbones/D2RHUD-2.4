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
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\buff-icons-test.exe" "%~dp0buff-icons-test.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" /link /LIBPATH:"%dependencyRoot%\lib" imgui.lib casc.lib zlib.lib user32.lib advapi32.lib wininet.lib
if errorlevel 1 exit /b 1
if "%~3"=="" goto unit
"%outputRoot%\buff-icons-test.exe" "%~3" "%~4"
exit /b %errorlevel%
:unit
"%outputRoot%\buff-icons-test.exe"
exit /b %errorlevel%
:usage
echo Usage: tests\run-buff-icons.cmd ^<x64-windows-static dependencies^> ^<output directory^> [D2R install directory] [mod name]
exit /b 1
