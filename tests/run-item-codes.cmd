@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\item-codes-test.exe" "%~dp0item-codes-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\item-codes-test.exe" %2 %3
exit /b %errorlevel%
:usage
echo Usage: tests\run-item-codes.cmd ^<output directory^> [D2R install directory] [loose mod name]
exit /b 1
