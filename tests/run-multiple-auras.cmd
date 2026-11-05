@echo off
setlocal
if "%~2"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\multiple-auras-test.exe" "%~dp0multiple-auras-test.cpp" /link user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\multiple-auras-test.exe" "%~2"
exit /b %errorlevel%
