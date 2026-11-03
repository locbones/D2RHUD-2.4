@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /DNDEBUG /DUNICODE /D_UNICODE /Fo"%outputRoot%\\" /Fe"%outputRoot%\pattern-scan-test.exe" "%~dp0pattern-scan-test.cpp" "%~dp0..\d2rhud\Pattern.cpp" "%~dp0..\d2rhud\ProcessPatternScan.cpp" /link psapi.lib
if errorlevel 1 exit /b 1
"%outputRoot%\pattern-scan-test.exe" %2
exit /b %errorlevel%
:usage
echo Usage: tests\run-pattern-scan.cmd ^<output directory^> [D2R.exe]
exit /b 1
