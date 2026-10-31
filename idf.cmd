@echo off
rem ---------------------------------------------------------------------------
rem  idf.cmd - run idf.py in the ESP-IDF environment of this machine.
rem
rem  The Espressif Installation Manager (EIM) does not put idf.py on PATH; the
rem  environment normally comes from
rem      C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
rem  which PowerShell refuses to load while the execution policy is Restricted.
rem  A .cmd file is not affected by the PowerShell execution policy, so this
rem  wrapper works from any plain cmd.exe or PowerShell window.
rem
rem  Usage:
rem      .\idf build
rem      .\idf -p COM8 flash monitor
rem      .\idf fullclean
rem
rem  Paths are the ones installed on this PC (see C:\Espressif\tools\eim_idf.json).
rem  After changing the ESP-IDF version with the EIM, update them here.
rem ---------------------------------------------------------------------------
setlocal

set "IDF_PATH=C:\esp\v6.1\esp-idf"
set "IDF_TOOLS_PATH=C:\Espressif\tools"
set "IDF_PYTHON_ENV_PATH=C:\Espressif\tools\python\v6.1\venv"
set "ESP_IDF_VERSION=6.1"
set "IDF_VERSION=6.1.0"
set "ESP_ROM_ELF_DIR=C:\Espressif\tools\esp-rom-elfs\20241011\"
set "PYTHONUTF8=1"

rem ccache is optional; keep it off so paths in compile_commands.json stay readable
set "IDF_CCACHE_ENABLE=0"

set "TOOLS=C:\Espressif\tools"
set "PATH=%TOOLS%\cmake\4.0.3\bin;%TOOLS%\ninja\1.12.1;%TOOLS%\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;%PATH%"

"%TOOLS%\python\v6.1\venv\Scripts\python.exe" "C:\esp\v6.1\esp-idf\tools\idf.py" %*
exit /b %ERRORLEVEL%
