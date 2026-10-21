@echo off
setlocal
if "%~2"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /DNOMINMAX /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\table-metadata-export-test.exe" "%~dp0table-metadata-export-test.cpp" "%~dp0..\d2rhud\plugin\TableMetadataExport.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib shell32.lib
if errorlevel 1 exit /b 1
"%outputRoot%\table-metadata-export-test.exe" "%~f2" -direct -txt
exit /b %errorlevel%
