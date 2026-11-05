@echo off
setlocal
if "%~2"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%~f1\\" /Fe"%~f1\marketplace-page-glow-native-test.exe" "%~dp0marketplace-page-glow-native-test.cpp" /link user32.lib
if errorlevel 1 exit /b 1
"%~f1\marketplace-page-glow-native-test.exe" "%~2"
if errorlevel 1 exit /b 1
"%~f1\marketplace-page-glow-native-test.exe" "%~2" corrupt
exit /b %errorlevel%
