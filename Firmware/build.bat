@echo off
setlocal
cd /d "%~dp0"

set "MODE=%~1"
if not "%MODE%"=="" goto :normalize_mode

echo ==========================================================
echo   Chon che do build Firmware ESP32-S sEMG:
echo     [1] RELEASE  - Mach PCB that (ADC1 DMA: GPIO 36,39,34)
echo     [2] TESTING  - Mo phong tap Test an (nut BOOT doi cu chi)
echo ==========================================================
set /p CHOICE="Nhap lua chon (1 hoac 2, mac dinh 1): "
if "%CHOICE%"=="2" (
    set "MODE=TESTING"
) else (
    set "MODE=RELEASE"
)

:normalize_mode
if /i "%MODE%"=="testing" set "MODE=TESTING"
if /i "%MODE%"=="release" set "MODE=RELEASE"
if /i "%MODE%"=="2" set "MODE=TESTING"
if /i "%MODE%"=="1" set "MODE=RELEASE"

echo ==========================================================
echo   Building ESP32-S sEMG Firmware [MODE: %MODE%]
echo ==========================================================

set "IDF_TOOLS_PATH=C:\Espressif"
set "IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.4.2"
set "PATH=C:\Espressif\python_env\idf5.4_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;%PATH%"

call C:\Espressif\idf_cmd_init.bat esp-idf-8db3121c30f24411ef389e0fd446d81c

python "%IDF_PATH%\tools\idf.py" -DFIRMWARE_MODE=%MODE% build
set "BUILD_RC=%ERRORLEVEL%"

if %BUILD_RC% EQU 0 (
    echo.
    echo ==========================================================
    echo   [BUILD SUCCESS] Mode: %MODE% -^> Firmware\build\
    echo ==========================================================
    exit /b 0
) else (
    echo.
    echo [BUILD FAILED] Mode: %MODE% - Return code: %BUILD_RC%
    exit /b %BUILD_RC%
)
