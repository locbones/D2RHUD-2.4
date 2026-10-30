@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++17 /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-library-test.exe" "%~dp0lootfilter-library-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-library-test.exe" "%outputRoot%" %2
exit /b %errorlevel%
