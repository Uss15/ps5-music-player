@echo off
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (
  py -3 host\music_web.py --open-desktop
) else (
  python host\music_web.py --open-desktop
)
