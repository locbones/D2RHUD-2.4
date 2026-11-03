@echo off
setlocal
if "%~1"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
python "%~dp0extract-d3d12-reset-test.py" "%~dp0..\d2rhud\D3D12Hook.cpp" "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /permissive- /EHsc /O2 /GL /MD /utf-8 /I"%outputRoot%" /Fo"%outputRoot%\\" /Fe"%outputRoot%\d3d12-reset-test.exe" "%~dp0d3d12-reset-test.cpp" "%~dp0..\d2rhud\ThemeImageRequests.cpp" /link /LTCG /INCREMENTAL:NO d3d12.lib dxgi.lib
if errorlevel 1 exit /b 1
"%outputRoot%\d3d12-reset-test.exe"
exit /b %errorlevel%
