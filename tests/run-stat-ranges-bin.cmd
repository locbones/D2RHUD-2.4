@echo off
setlocal
if "%~1"=="" exit /b 1
if "%~2"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\stat-ranges-bin-test.exe" "%~dp0stat-ranges-bin-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\stat-ranges-bin-test.exe" "%~f2"
exit /b %errorlevel%
