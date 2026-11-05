@echo off
setlocal
if "%~1"=="" exit /b 1
set "outputRoot=%~f1"
if not exist "%outputRoot%" mkdir "%outputRoot%"
cl /nologo /std:c++latest /EHsc /O2 /MT /utf-8 /Fo"%outputRoot%\\" /Fe"%outputRoot%\gift-codec-test.exe" "%~dp0marketplace-gift-codec-test.cpp"
exit /b %errorlevel%
