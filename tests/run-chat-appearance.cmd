@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\chat-appearance-test.exe" "%~dp0chat-appearance-test.cpp" /link user32.lib
if errorlevel 1 exit /b 1
"%~f1\chat-appearance-test.exe"
exit /b %errorlevel%
