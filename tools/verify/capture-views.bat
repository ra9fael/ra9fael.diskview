@echo off
setlocal
set PATH=C:\Windows\System32;D:\tools\Qt\6.12.0\msvc2022_64\bin;%PATH%
cd /d D:\dev\github\winshun-dev\ra9fael.diskview
set WS=D:\dev\github\winshun-dev\WinShun\build\debug\wsbench.exe
set CAP=D:\dev\github\winshun-dev\ra9fael.diskview\build\capture-window.ps1
set PL=D:\dev\github\winshun-dev\ra9fael.diskview

echo === treemap ===
start "" /b "%WS%" --plugin "%PL%" --query "disk D:\dev\github\winshun-dev\ra9fael.diskview" --action open --linger 9000 --runs 1 > build\wsbench-treemap.log 2>&1
ping -n 6 127.0.0.1 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%CAP%" -Out "%PL%\build\treemap.png"
timeout /t 6 /nobreak >nul

echo === sunburst ===
start "" /b "%WS%" --plugin "%PL%" --query "disk D:\dev\github\winshun-dev\ra9fael.diskview" --action open --linger 9000 --runs 1 --setting defaultView=sunburst > build\wsbench-sunburst.log 2>&1
ping -n 6 127.0.0.1 >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%CAP%" -Out "%PL%\build\sunburst.png"
timeout /t 6 /nobreak >nul

echo === done ===
endlocal
