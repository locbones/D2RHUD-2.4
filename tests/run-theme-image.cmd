@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
python "%~dp0extract-theme-image-test.py" "%~dp0..\d2rhud\D3D12Hook.cpp" "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /permissive- /EHsc /O2 /MT /utf-8 /I"%outputRoot%" /Fo"%outputRoot%\\" /Fe"%outputRoot%\theme-image-test.exe" "%~dp0theme-image-test.cpp" "%~dp0..\d2rhud\ThemeImageRequests.cpp" /link d3d12.lib dxgi.lib
if errorlevel 1 exit /b 1
"%outputRoot%\theme-image-test.exe" "%outputRoot%"
exit /b %errorlevel%
