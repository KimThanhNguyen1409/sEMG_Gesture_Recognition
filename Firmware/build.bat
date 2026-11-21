@echo off
setlocal
cd /d "%~dp0"

echo ==========================================================
echo   Building ESP32-S sEMG Firmware (ESP-IDF v5.4.2)
echo ==========================================================

:: Initialize ESP-IDF environment using the configured Python 3.11 environment
set "IDF_TOOLS_PATH=C:\Espressif"
set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.4.2"
set "PATH=C:\Espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;%PATH%"

call C:\Espressif\idf_cmd_init.bat esp-idf-8db3121c30f24411ef389e0fd446d81c

python "%IDF_PATH%\tools\idf.py" build

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ==========================================================
    echo   [BUILD SUCCESS] Binary generated in Firmware\build\
    echo ==========================================================
) else (
    echo.
    echo [BUILD FAILED] Return code: %ERRORLEVEL%
)
