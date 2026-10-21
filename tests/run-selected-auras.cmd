@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\selected-auras-test.exe" "%~dp0selected-auras-test.cpp"
if errorlevel 1 exit /b 1
if "%~2"=="" goto unit
"%outputRoot%\selected-auras-test.exe" "%~2"
exit /b %errorlevel%
:unit
"%outputRoot%\selected-auras-test.exe"
exit /b %errorlevel%
:usage
echo Usage: tests\run-selected-auras.cmd ^<output directory^> [audited D2R.exe]
exit /b 1
