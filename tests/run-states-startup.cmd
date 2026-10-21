@echo off
setlocal
if "%~3"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /WX /Fo"%outputRoot%\\" /Fe"%outputRoot%\states-startup-test.exe" "%~dp0states-startup-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\states-startup-test.exe" %2 %3
exit /b %errorlevel%
