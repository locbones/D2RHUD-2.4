@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist build-marketplace\chat-tests mkdir build-marketplace\chat-tests
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /Fo"build-marketplace\chat-tests\chat.obj" /Fe"build-marketplace\chat-tests\chat.exe" tests\marketplace-chat-test.cpp
if errorlevel 1 exit /b 1
build-marketplace\chat-tests\chat.exe "%TEMP%\market-chat-%RANDOM%-%RANDOM%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /Fo"build-marketplace\chat-tests\presence.obj" /Fe"build-marketplace\chat-tests\presence.exe" tests\marketplace-chat-presence-test.cpp
if errorlevel 1 exit /b 1
build-marketplace\chat-tests\presence.exe "%TEMP%\market-presence-%RANDOM%-%RANDOM%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /Fo"build-marketplace\chat-tests\cache.obj" /Fe"build-marketplace\chat-tests\cache.exe" tests\marketplace-chat-cache-test.cpp
if errorlevel 1 exit /b 1
build-marketplace\chat-tests\cache.exe "%TEMP%\market-cache-%RANDOM%-%RANDOM%"
exit /b %errorlevel%
