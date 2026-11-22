@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\NEXTALK-Refactor\.local\wsl-demo\Start-NEXTALK-WSL.ps1" -Single
exit /b %ERRORLEVEL%
