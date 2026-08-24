@echo off
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if %errorLevel% neq 0 (
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

if not exist "bin\WinDivert.dll" copy "libs\WinDivert.dll" "bin\" >nul
if not exist "bin\WinDivert64.sys" copy "libs\WinDivert64.sys" "bin\" >nul

set PATH=%~dp0libs;%PATH%
title Fearverk DPI - USA Edge
bin\FearverkDPI.exe --country US
pause