@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\inspect-test.exe" "%~dp0inspect-test.cpp" /link user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\inspect-test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\inspect-native-test.exe" "%~dp0inspect-native-test.cpp" "%~dp0..\d2rhud\plugin\Inspect.cpp" /link user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\inspect-native-test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
if "%~2"=="" exit /b 0
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\inspect-world-test.exe" "%~dp0inspect-world-test.cpp" /link user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\inspect-world-test.exe" "%~f2"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\inspect-selection-test.exe" "%~dp0inspect-selection-test.cpp" /link /LIBPATH:"%deps%\lib" minhook.x64.lib user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\inspect-selection-test.exe" "%~f2"
exit /b %errorlevel%
