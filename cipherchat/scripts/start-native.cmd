@echo off
setlocal
rem Optional native Qt Rhine entry. The default UI uses start-messager.cmd.
set "ROOT=%~dp0..\"
set "CLIENT=%ROOT%framework\build\verification\cipherchat.exe"
set "CA=%ROOT%artifacts\CipherChat-Remote-Windows-x64-codex-20260929\server-cert.pem"
set "PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.2\mingw_64\bin;D:\msys64\mingw64\bin;%PATH%"
if not defined CIPHERCHAT_DATA_DIR set "CIPHERCHAT_DATA_DIR=%LOCALAPPDATA%\CipherChat\remote-server2"
for %%F in ("%CLIENT%" "%CA%") do if not exist "%%~F" (
    echo Missing file: %%~F
    exit /b 1
)
start "" "%CLIENT%" --ui rhine --host 2603:1040:f02:d::21d --port 55220 --server-name 2603:1040:f02:d::21d --ca "%CA%" --data-dir "%CIPHERCHAT_DATA_DIR%" %*
exit /b %ERRORLEVEL%
