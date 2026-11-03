@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\lootfilter-catalog-test.exe" "%~dp0lootfilter-catalog-test.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\LootFilterCatalog.cpp"
if errorlevel 1 exit /b 1
"%outputRoot%\lootfilter-catalog-test.exe" "%outputRoot%"
exit /b %errorlevel%
