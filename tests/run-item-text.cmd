@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%~f1\\" /Fe"%~f1\item-text-test.exe" "%~dp0item-text-test.cpp"
if errorlevel 1 exit /b 1
"%~f1\item-text-test.exe"
exit /b %errorlevel%
