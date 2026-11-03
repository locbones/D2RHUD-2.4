@echo off
setlocal
if "%~1"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /permissive- /EHsc /O2 /GL /MD /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\process-lifetime-test.exe" "%~dp0process-lifetime-test.cpp" /link /LTCG /INCREMENTAL:NO
if errorlevel 1 exit /b 1
"%outputRoot%\process-lifetime-test.exe"
exit /b %errorlevel%
