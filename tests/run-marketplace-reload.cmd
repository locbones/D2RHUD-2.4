@echo off
setlocal
if "%~1"=="" exit /b 1
if not exist "%~f1" mkdir "%~f1"
python "%~dp0generate-marketplace-reload-test.py" "%~f1\reload-fixture.cpp"
if errorlevel 1 exit /b 1
cl /nologo /std:c++latest /EHsc /O2 /MT /W4 /utf-8 /I"%~dp0.." /Fo"%~f1\\" /Fe"%~f1\reload-fixture.exe" "%~f1\reload-fixture.cpp"
if errorlevel 1 exit /b 1
"%~f1\reload-fixture.exe"
exit /b %errorlevel%
