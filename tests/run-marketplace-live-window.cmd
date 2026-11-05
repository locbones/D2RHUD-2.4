@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if %errorlevel% neq 0 exit /b %errorlevel%
if not exist build-marketplace\live-tests mkdir build-marketplace\live-tests
cl /nologo /std:c++20 /EHsc /O2 /MT /Fo"build-marketplace\live-tests\window.obj" /Fe"build-marketplace\live-tests\window.exe" tests\marketplace-live-window-test.cpp
if %errorlevel% neq 0 exit /b %errorlevel%
build-marketplace\live-tests\window.exe "%TEMP%\d2-market-live-%RANDOM%-%RANDOM%" "%~1"
if %errorlevel% neq 0 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /O2 /MT /Fo"build-marketplace\live-tests\withdrawal-input.obj" /Fe"build-marketplace\live-tests\withdrawal-input.exe" tests\marketplace-withdrawal-input-test.cpp /link user32.lib
if %errorlevel% neq 0 exit /b %errorlevel%
build-marketplace\live-tests\withdrawal-input.exe
if %errorlevel% neq 0 exit /b %errorlevel%
if "%~2"=="" exit /b 0
cl /nologo /std:c++17 /EHsc /O2 /MT /Fo"build-marketplace\live-tests\page-packets.obj" /Fe"build-marketplace\live-tests\page-packets.exe" tests\marketplace-page-packets-test.cpp
if %errorlevel% neq 0 exit /b %errorlevel%
build-marketplace\live-tests\page-packets.exe "%~2"
exit /b %errorlevel%
