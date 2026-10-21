@echo off
setlocal
if "%~2"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\catalog-bin-test.exe" "%~dp0catalog-bin-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\catalog-bin-test.exe" "%~f2"
exit /b %errorlevel%
