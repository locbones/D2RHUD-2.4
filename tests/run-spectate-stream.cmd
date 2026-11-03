@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "LOCALAPPDATA=%~f1\profile"
if not exist "%LOCALAPPDATA%" mkdir "%LOCALAPPDATA%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /Fo"%~f1\\" /Fe"%~f1\spectate-stream-test.exe" "%~dp0spectate-stream-test.cpp" "%~dp0..\d2rhud\plugin\SpectateTransport.cpp" "%~dp0..\d2rhud\plugin\SpectateH264.cpp" "%~dp0spectate-jpeg-baseline.cpp" /link user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-stream-test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /Fo"%~f1\\" /Fe"%~f1\spectate-stream-wire-test.exe" "%~dp0spectate-stream-wire-test.cpp"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-stream-wire-test.exe"
exit /b %errorlevel%
