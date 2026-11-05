@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%~f1\\" /Fe"%~f1\hitch-diagnostics-test.exe" "%~dp0hitch-diagnostics-test.cpp" "%~dp0..\d2rhud\HitchDiagnostics.cpp" /link user32.lib
if errorlevel 1 exit /b 1
"%~f1\hitch-diagnostics-test.exe"
exit /b %errorlevel%
