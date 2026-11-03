@echo off
setlocal
if "%~1"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static"
if not exist "%deps%\include\lua.hpp" set "deps=%deps%\x64-windows-static"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /W4 /utf-8 /DNDEBUG /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-updater-test.exe" "%~dp0lootfilter-updater-test.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\LootFilterUpdater.cpp" /link /LIBPATH:"%deps%\lib" lua.lib winhttp.lib ws2_32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-updater-test.exe" %2
exit /b %errorlevel%
:usage
echo Usage: tests\run-lootfilter-updater.cmd ^<output directory^> [engine.lua]
exit /b 1
