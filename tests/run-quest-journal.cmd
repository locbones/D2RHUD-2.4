@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\quest-journal-test.exe" "%~dp0quest-journal-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\quest-journal-test.exe" %2 %3
exit /b %errorlevel%
