@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /Fo"%outputRoot%\\" /Fe"%outputRoot%\camera-controller-test.exe" "%~dp0camera-controller-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\camera-controller-test.exe"
exit /b %errorlevel%
:usage
echo Usage: tests\run-camera-controller.cmd ^<output directory^>
exit /b 1
