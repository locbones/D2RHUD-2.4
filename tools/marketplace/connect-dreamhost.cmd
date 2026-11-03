@echo off
rem A real console is required for native SSH trust and password prompts.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NoExit -File "%~dp0connect-dreamhost.ps1"
