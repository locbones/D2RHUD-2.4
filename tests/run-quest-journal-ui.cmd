@echo off
setlocal
if "%~6"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\quest-journal-ui-test.exe" "%~dp0quest-journal-ui-test.cpp" /link /LIBPATH:"%deps%\lib" lua.lib imgui.lib
if errorlevel 1 exit /b 1
"%outputRoot%\quest-journal-ui-test.exe" "%~f1" "%~f2" "%~f3" "%~f4" "%~f5" "%~f6" "%~f7"
exit /b %errorlevel%
