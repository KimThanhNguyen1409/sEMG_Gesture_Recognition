@echo off
setlocal
cd /d "%~dp0"

set PORT=%1
if "%PORT%"=="" (
    set /p PORT="Nhap cong COM cua ESP32-S (vi du COM3, COM4): "
)

echo ==========================================================
echo   Flashing ESP32-S sEMG Firmware on %PORT%
echo ==========================================================

:: Initialize ESP-IDF environment
set "IDF_TOOLS_PATH=C:\Espressif"
set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.4.2"
set "PATH=C:\Espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;%PATH%"

call C:\Espressif\idf_cmd_init.bat esp-idf-8db3121c30f24411ef389e0fd446d81c

python "%IDF_PATH%\tools\idf.py" -p %PORT% flash monitor
