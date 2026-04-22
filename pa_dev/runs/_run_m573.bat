@echo off
cd /d "C:\Users\Antdr\OneDrive\Computers\Work Computers\Computers\Prusa\Prusa-Firmware-Buddy"
set TS=20260422_153045
C:\Python312\python.exe -u pa_dev\tools\stream_gcode.py COM5 pa_dev\preflights\m573_capture.gcode --log pa_dev\runs\stream_m573_%TS%.log --quiet-rx > pa_dev\runs\stream_m573_%TS%.stdout 2> pa_dev\runs\stream_m573_%TS%.stderr
echo EXITCODE=%ERRORLEVEL%
