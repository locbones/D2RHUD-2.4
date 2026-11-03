@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\item-sprite-cache-test.exe" "%~dp0item-sprite-cache-test.cpp" "%~dp0..\d2rhud\plugin\ItemSpriteCache.cpp" "%~dp0..\d2rhud\plugin\StackSplitSkin.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\item-sprite-cache-test.exe"
exit /b %errorlevel%
