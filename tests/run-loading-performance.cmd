@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\loading-performance-test.exe" "%~dp0loading-performance-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\loading-performance-test.exe" %2
exit /b %errorlevel%
:usage
echo Usage: tests\run-loading-performance.cmd ^<output directory^> [audited D2R.exe]
exit /b 1
