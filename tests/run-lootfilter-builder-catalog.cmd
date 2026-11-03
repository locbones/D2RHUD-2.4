@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static"
if not exist "%deps%\include\lua.hpp" set "deps=%deps%\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-builder-catalog-test.exe" "%~dp0lootfilter-builder-catalog-test.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\LootFilterBuilderCatalog.cpp" /link /LIBPATH:"%deps%\lib" lua.lib
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-builder-catalog-test.exe"
exit /b %errorlevel%
