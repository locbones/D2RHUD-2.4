@echo off
setlocal
if "%~1"=="" goto usage
if "%~2"=="" goto usage
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "dependencyRoot=%~f1"
set "outputRoot=%~f2"
if not exist "%outputRoot%" mkdir "%outputRoot%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-config-test.exe" "%~dp0lootfilter-config-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" lua.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-config-test.exe" %3
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-builder-test.exe" "%~dp0lootfilter-builder-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" lua.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-builder-test.exe" %4
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-preview-test.exe" "%~dp0lootfilter-preview-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" lua.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-preview-test.exe" %4
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-editor-test.exe" "%~dp0lootfilter-editor-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" lua.lib imgui.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-editor-test.exe" "" %4
if errorlevel 1 exit /b 1
if "%~4"=="" exit /b 0
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /bigobj /I "%dependencyRoot%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-runtime-test.exe" "%~dp0lootfilter-runtime-test.cpp" /link /LIBPATH:"%dependencyRoot%\lib" lua.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-runtime-test.exe" "%~4"
exit /b %errorlevel%
:usage
echo Usage: tests\run-lootfilter.cmd ^<x64-windows-static dependencies^> ^<output directory^> [D2RLAN-Filters directory] [lootfilter.lua]
exit /b 1
