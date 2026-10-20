@echo off
setlocal
echo Close CipherChat before rolling back to the previous version.
choice /c YN /m "Roll back now?"
if errorlevel 2 exit /b 0
"%~dp0cipherchat-updater.exe" --rollback "%~dp0."
if errorlevel 1 pause
