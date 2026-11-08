@echo off
setlocal
set PATH=C:\Windows\System32;D:\tools\Qt\6.12.0\msvc2022_64\bin;%PATH%
cd /d D:\dev\github\winshun-dev\ra9fael.diskview
set WS=D:\dev\github\winshun-dev\WinShun\build\debug\wsbench.exe
set PL=D:\dev\github\winshun-dev\ra9fael.diskview
set FIX=C:\Users\renfei\AppData\Local\Temp\diskview_probe

echo === drill into the folder ===
start "" /b "%WS%" --plugin "%PL%" --query "disk %FIX%" --action open --linger 12000 --runs 1 > build\wsbench-drill.log 2>&1
ping -n 6 127.0.0.1 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%PL%\build\click-window.ps1" -Out "%PL%\build\drill.png" -ClickX 400 -ClickY 300
timeout /t 8 /nobreak >nul
echo === done ===
endlocal
