@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\deferred-assets-test.exe" "%~dp0deferred-assets-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\deferred-assets-test.exe"
exit /b %errorlevel%
