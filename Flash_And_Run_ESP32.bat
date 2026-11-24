@echo off
setlocal enabledelayedexpansion
title ESP32 Smart Room Monitor - 1-Click Flash & Monitor
color 0b

echo ===============================================================================
echo        ESP32 Smart Room Monitoring & Gas Detection System
echo                  1-Click Auto Flash and Monitor Tool
echo ===============================================================================
echo.

set "SCRIPT_DIR=%~dp0"
set "BIN_FILE="

if exist "%SCRIPT_DIR%smart_home_monitoring_merged_0x0.bin" (
    set "BIN_FILE=%SCRIPT_DIR%smart_home_monitoring_merged_0x0.bin"
) else if exist "%SCRIPT_DIR%smart_home_automation_merged.bin" (
    set "BIN_FILE=%SCRIPT_DIR%smart_home_automation_merged.bin"
) else if exist "%SCRIPT_DIR%smart_house_monitoring_merged.bin" (
    set "BIN_FILE=%SCRIPT_DIR%smart_house_monitoring_merged.bin"
) else if exist "%USERPROFILE%\OneDrive\Desktop\smart_home_monitoring_merged_0x0.bin" (
    set "BIN_FILE=%USERPROFILE%\OneDrive\Desktop\smart_home_monitoring_merged_0x0.bin"
) else if exist "%USERPROFILE%\OneDrive\Desktop\smart_house_monitoring_merged.bin" (
    set "BIN_FILE=%USERPROFILE%\OneDrive\Desktop\smart_house_monitoring_merged.bin"
) else (
    echo [ERROR] Firmware binary not found!
    echo Please make sure 'smart_home_monitoring_merged_0x0.bin' is in this folder.
    pause
    exit /b 1
)

echo [*] Firmware Binary Located:
echo     %BIN_FILE%
echo.

echo [*] Connected COM Ports:
powershell -Command "[System.IO.Ports.SerialPort]::GetPortNames()"
echo.

set /p COMPORT="Enter COM port (e.g. COM3, COM4) or press ENTER for Auto-Detect: "

set "PORT_PARAM="
if not "%COMPORT%"=="" (
    set "PORT_PARAM=--port %COMPORT%"
)

echo.
echo [*] Ready to flash to ESP32 at offset 0x0...
echo [TIP] If it stays at 'Connecting........_____', press and hold the 'BOOT' button!
echo.
pause

echo.
echo ===============================================================================
echo [*] FLASHING ESP32 FIRMWARE...
echo ===============================================================================
python -m esptool --chip esp32 %PORT_PARAM% --baud 921600 write_flash 0x0 "%BIN_FILE%"

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ===============================================================================
    echo [!] High speed flashing failed. Retrying at safe speed (115200 baud)...
    echo ===============================================================================
    python -m esptool --chip esp32 %PORT_PARAM% --baud 115200 write_flash 0x0 "%BIN_FILE%"
)

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Flashing failed!
    echo - Verify USB cable is plugged in firmly.
    echo - Make sure no other program (like Arduino IDE / PuTTY) has the COM port open.
    pause
    exit /b 1
)

echo.
echo ===============================================================================
echo [SUCCESS] Flashing finished! Opening Serial Monitor...
echo ===============================================================================
echo [NOTE] Press Ctrl+] at any time to exit the serial monitor.
echo.
timeout /t 2 /nobreak >nul

if not "%COMPORT%"=="" (
    python -m serial.tools.miniterm --raw %COMPORT% 115200
) else (
    for /f "tokens=*" %%a in ('powershell -Command "[System.IO.Ports.SerialPort]::GetPortNames() | Select-Object -First 1"') do (
        python -m serial.tools.miniterm --raw %%a 115200
    )
)

pause
