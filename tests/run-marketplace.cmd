@echo off
setlocal
if "%~1"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\marketplace-test.exe" "%~dp0marketplace-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\marketplace-test.exe" "%outputRoot%\data" %2 %3
exit /b %errorlevel%
