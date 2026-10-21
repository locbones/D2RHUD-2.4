@echo off
setlocal
if "%~1"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\terror-zone-rotation-test.exe" "%~dp0terror-zone-rotation-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\terror-zone-rotation-test.exe"
exit /b %errorlevel%
