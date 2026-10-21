@echo off
setlocal
if "%~1"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\crash-diagnostics-test.exe" "%~dp0crash-diagnostics-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\crash-diagnostics-test.exe" "%outputRoot%\capture"
exit /b %errorlevel%
