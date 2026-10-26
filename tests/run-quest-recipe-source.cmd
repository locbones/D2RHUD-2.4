@echo off
setlocal
if "%~2"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\quest-recipe-source-test.exe" "%~dp0quest-recipe-source-test.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" /link /LIBPATH:"%deps%\lib" casc.lib zlib.lib
if errorlevel 1 exit /b 1
"%outputRoot%\quest-recipe-source-test.exe" "%~f2"
exit /b %errorlevel%
