@echo off
setlocal
if "%~2"=="" exit /b 1
set "deps=%~f1"
set "outputRoot=%~f2"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\merc-icons-test.exe" "%~dp0merc-icons-test.cpp" "%~dp0..\d2rhud\plugin\GameAssets.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib casc.lib zlib.lib user32.lib advapi32.lib wininet.lib
if errorlevel 1 exit /b 1
"%outputRoot%\merc-icons-test.exe" %3 %4 %5
exit /b %errorlevel%
