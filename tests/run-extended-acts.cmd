@echo off
setlocal
if "%~2"=="" exit /b 1
where cl >nul 2>nul
if errorlevel 1 exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /I"%deps%\include" /Fo"%outputRoot%\\" /Fe"%outputRoot%\extended-acts-test.exe" "%~dp0extended-acts-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%outputRoot%\extended-acts-test.exe" "%~2" "%~3"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\act6-native-storage-test.exe" "%~dp0act6-native-storage-test.cpp"
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%outputRoot%\act6-native-storage-test.exe" "%~2"
if not "%errorlevel%"=="0" exit /b %errorlevel%
if "%~3"=="" exit /b 0
python "%~dp0stage-act6-test.py" "%~3"
if not "%errorlevel%"=="0" exit /b %errorlevel%
python "%~dp0act6-town-test.py"
exit /b %errorlevel%
