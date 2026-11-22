@echo off
rem The settings + flashing page on this computer only: http://localhost:8000/ in Chrome or Edge (Web Serial needs one of them).
rem Needs nothing installed: Python 3 if there is one (py launcher, then python), else the PowerShell server next to this file (serve.ps1).  127.0.0.1 only.
cd /d "%~dp0"
set URL=http://localhost:8000/
echo %URL%  (Ctrl+C to stop)
start "" cmd /c "timeout /t 2 /nobreak >nul & start http://localhost:8000/"
py -3 -c "import http.server" >nul 2>&1 && ( py -3 -m http.server 8000 --bind 127.0.0.1 & goto :eof )
python -c "import http.server" >nul 2>&1 && ( python -m http.server 8000 --bind 127.0.0.1 & goto :eof )
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0serve.ps1"
