@echo off
set "PATH=C:\Program Files\mosquitto;C:\mingw64\bin;%PATH%"
cd /d "%~dp0"

taskkill /F /IM semg_server.exe >nul 2>&1
del /F /Q semg_server_old.exe >nul 2>&1

echo [BUILD] Compiling semg_server.exe (MQTT + TFLite + Web GUI Server)...
gcc src/main.c -Iinc -I"C:\Program Files\mosquitto\devel" "C:\Program Files\mosquitto\mosquitto.dll" "C:\Program Files\mosquitto\mosquitto_common.dll" -lws2_32 -O2 -Wall -Wextra -o semg_server.exe
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo [RUN] Starting sEMG C Inference Server (Web GUI: http://localhost:8080)...
start "" "http://localhost:8080"
semg_server.exe %*
