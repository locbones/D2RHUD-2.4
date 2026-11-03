@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
set "LOCALAPPDATA=%~f1\profile"
if not exist "%LOCALAPPDATA%" mkdir "%LOCALAPPDATA%"
set "deps=%~dp0..\d2rhud\vcpkg_installed\x64-windows-static\x64-windows-static"
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\spectate-test.exe" "%~dp0spectate-test.cpp" "%~dp0..\d2rhud\plugin\SpectateH264.cpp" "%~dp0..\d2rhud\plugin\SpectateTransport.cpp" /link user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\spectate-video-test.exe" "%~dp0spectate-video-test.cpp" "%~dp0..\d2rhud\plugin\SpectateH264.cpp" "%~dp0..\d2rhud\plugin\SpectateTransport.cpp" /link d3d12.lib dxgi.lib user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-video-test.exe"
if not "%errorlevel%"=="0" exit /b %errorlevel%
if "%~2"=="" exit /b 0
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\spectate-native-test.exe" "%~dp0spectate-native-test.cpp" "%~dp0..\d2rhud\plugin\Spectate.cpp" "%~dp0..\d2rhud\plugin\SpectateTransport.cpp" /link /LIBPATH:"%deps%\lib" detours.lib user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-native-test.exe" "%~f2"
if not "%errorlevel%"=="0" exit /b %errorlevel%
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /DUNICODE /D_UNICODE /I"%deps%\include" /Fo"%~f1\\" /Fe"%~f1\spectate-ui-test.exe" "%~dp0spectate-ui-test.cpp" "%~dp0..\d2rhud\plugin\D2RHUD\HudConfigStore.cpp" "%~dp0..\d2rhud\plugin\SpectateTransport.cpp" /link /LIBPATH:"%deps%\lib" imgui.lib user32.lib
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%~f1\spectate-ui-test.exe" "%~f1" "%~dp0..\d2rhud\fonts\0.otf"
exit /b %errorlevel%

