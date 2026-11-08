@echo off
setlocal
set PATH=C:\Windows\System32;D:\tools\Qt\6.12.0\msvc2022_64\bin;%PATH%
cd /d D:\dev\github\winshun-dev\ra9fael.diskview
set WS=D:\dev\github\winshun-dev\WinShun\build\debug\wsbench.exe
set CAP=D:\dev\github\winshun-dev\ra9fael.diskview\build\capture-window.ps1
set PL=D:\dev\github\winshun-dev\ra9fael.diskview

echo === missing path ===
start "" /b "%WS%" --plugin "%PL%" --query "disk Z:\__diskview_missing__" --action open --linger 9000 --runs 1 > build\wsbench-missing.log 2>&1
ping -n 6 127.0.0.1 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%CAP%" -Out "%PL%\build\missing.png"
timeout /t 6 /nobreak >nul

echo === empty folder ===
mkdir "%TEMP%\diskview_empty_probe" 2>nul
start "" /b "%WS%" --plugin "%PL%" --query "disk %TEMP%\diskview_empty_probe" --action open --linger 9000 --runs 1 > build\wsbench-empty.log 2>&1
ping -n 6 127.0.0.1 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%CAP%" -Out "%PL%\build\empty.png"
timeout /t 6 /nobreak >nul
rmdir "%TEMP%\diskview_empty_probe" 2>nul

echo === done ===
endlocal
