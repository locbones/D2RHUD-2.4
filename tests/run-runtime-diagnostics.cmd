@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\runtime-diagnostics-test.exe" "%~dp0runtime-diagnostics-test.cpp" "%~dp0..\d2rhud\RuntimeDiagnostics.cpp" "%~dp0..\d2rhud\HitchDiagnostics.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\HudConfigStore.cpp" "%~dp0..\d2rhud\plugin\TooltipText.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib advapi32.lib user32.lib
if errorlevel 1 exit /b 1
"%~f1\runtime-diagnostics-test.exe"
exit /b %errorlevel%
