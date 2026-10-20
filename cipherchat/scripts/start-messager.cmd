@echo off
setlocal
set "LAUNCHER=%~dp0..\..\Messager\runtime-v0.3.1\CipherChat-UI-Selector.exe"
if not exist "%LAUNCHER%" exit /b 1
start "" "%LAUNCHER%" --ui messager -- %*
exit /b %ERRORLEVEL%
