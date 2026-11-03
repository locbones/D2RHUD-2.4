@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\marketplace-ui-test.exe" "%~dp0marketplace-ui-test.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib user32.lib gdi32.lib imm32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\marketplace-ui-test.exe" "%outputRoot%\run-%RANDOM%-%RANDOM%" %2
exit /b %errorlevel%
