@echo off
setlocal
if "%~5"=="" exit /b 1
set "deps=%~f1"
set "out=%~f2"
if not exist "%out%" mkdir "%out%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%out%\\" /Fe"%out%\merc-animation-test.exe" "%~dp0merc-animation-test.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" /link /LIBPATH:"%deps%\lib" casc.lib zlib.lib
if errorlevel 1 exit /b 1
"%out%\merc-animation-test.exe" %3 %4 %5
exit /b %errorlevel%
