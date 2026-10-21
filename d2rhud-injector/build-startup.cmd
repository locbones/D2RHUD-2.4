@echo off
setlocal
if "%~1"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++17 /EHsc /W4 /WX /MT /O2 /Fo"%outputRoot%\\" /Fe"%outputRoot%\Pandemonium.HudLoader.exe" "%~dp0HudLoader.cpp" /link user32.lib
exit /b %errorlevel%
