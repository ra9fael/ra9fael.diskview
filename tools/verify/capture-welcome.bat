@echo off
rem The empty plugin query selects a drive now. Use the deterministic window
rem harness to capture the welcome page without accidentally scanning a drive.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0render-views.ps1"
exit /b %errorlevel%
