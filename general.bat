@echo off
setlocal
cd /d "%~dp0"

:: Check Admin Rights
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [!] Elevating to Administrator...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

:: Ensure DLL & Drivers are synced in bin
if not exist "bin\WinDivert.dll" copy "libs\WinDivert.dll" "bin\" >nul
if not exist "bin\WinDivert64.sys" copy "libs\WinDivert64.sys" "bin\" >nul

set PATH=%~dp0libs;%PATH%
title Fearverk DPI - Auto Best Node
bin\FearverkDPI.exe --country AUTO
pause