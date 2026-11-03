@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /Fo"%~f1\\" /Fe"%~f1\spectate-h264-test.exe" "%~dp0spectate-h264-test.cpp"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-h264-test.exe"
exit /b %errorlevel%
