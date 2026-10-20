@echo off
setlocal
if not exist "%~dp0cipherchat-launcher.exe" (
  echo Please extract the complete CipherChat update package first.
  pause
  exit /b 1
)
start "" "%~dp0cipherchat-launcher.exe" %*
