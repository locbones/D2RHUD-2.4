@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\camera-editor-test.exe" "%~dp0camera-editor-test.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\CameraEditor.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\HudPresentation.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\CameraController.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\camera-editor-test.exe"
exit /b %errorlevel%
