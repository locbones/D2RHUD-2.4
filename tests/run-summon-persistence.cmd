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
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\summon-persistence-data-test.exe" "%~dp0summon-persistence-data-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\summon-persistence-data-test.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\summon-persistence-test.exe" "%~dp0summon-persistence-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" minhook.x64.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\summon-persistence-test.exe"
exit /b %errorlevel%
:usage
echo Usage: tests\run-summon-persistence.cmd ^<x64-windows-static dependencies^> ^<output directory^>
exit /b 1
