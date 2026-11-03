@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\hud-chat-sound-test.exe" "%~dp0hud-chat-sound-test.cpp" /link user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\hud-chat-sound-test.exe" "%outputRoot%"
exit /b %errorlevel%
