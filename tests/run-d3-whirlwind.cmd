@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\d3-whirlwind-test.exe" "%~dp0d3-whirlwind-test.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\d3-whirlwind-test.exe"
if errorlevel 1 exit /b 1
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\d3-whirlwind-core-test.exe" "%~dp0d3-whirlwind-core-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\d3-whirlwind-core-test.exe" %2
if errorlevel 1 exit /b 1
if "%~2"=="" exit /b 0
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\d3-whirlwind-install-test.exe" "%~dp0d3-whirlwind-install-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib user32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\d3-whirlwind-install-test.exe" "%~2"
exit /b %errorlevel%
:usage
echo Usage: tests\run-d3-whirlwind.cmd ^<output directory^> [audited D2R.exe]
exit /b 1
