@echo off
setlocal
cd /d "%~dp0"
if exist "runtime-v0.3.14\cipherchat-launcher.exe" (
  start "" "%~dp0runtime-v0.3.14\cipherchat-launcher.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.13\cipherchat-launcher.exe" (
  start "" "%~dp0runtime-v0.3.13\cipherchat-launcher.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.12\cipherchat-launcher.exe" (
  start "" "%~dp0runtime-v0.3.12\cipherchat-launcher.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.11\cipherchat-launcher.exe" (
  start "" "%~dp0runtime-v0.3.11\cipherchat-launcher.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.10\Messager.exe" (
  start "" "%~dp0runtime-v0.3.10\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.9\Messager.exe" (
  start "" "%~dp0runtime-v0.3.9\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.8\Messager.exe" (
  start "" "%~dp0runtime-v0.3.8\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.7\Messager.exe" (
  start "" "%~dp0runtime-v0.3.7\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.6\Messager.exe" (
  start "" "%~dp0runtime-v0.3.6\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.5\Messager.exe" (
  start "" "%~dp0runtime-v0.3.5\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.4\Messager.exe" (
  start "" "%~dp0runtime-v0.3.4\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.3\Messager.exe" (
  start "" "%~dp0runtime-v0.3.3\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.2\Messager.exe" (
  start "" "%~dp0runtime-v0.3.2\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3.1\Messager.exe" (
  start "" "%~dp0runtime-v0.3.1\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.3\Messager.exe" (
  start "" "%~dp0runtime-v0.3\Messager.exe" %*
  exit /b 0
)
if exist "runtime-v0.2\Messager.exe" (
  start "" "%~dp0runtime-v0.2\Messager.exe" %*
  exit /b 0
)
if exist "runtime\Messager.exe" (
  start "" "%~dp0runtime\Messager.exe" %*
  exit /b 0
)
if exist "work\build\Messager.exe" (
  if exist "D:\msys64\mingw64\bin\Qt6Core.dll" set "PATH=D:\msys64\mingw64\bin;%PATH%"
  start "" "%~dp0work\build\Messager.exe" %*
  exit /b 0
)
echo Please build the project first, or use the Windows portable package.
pause
