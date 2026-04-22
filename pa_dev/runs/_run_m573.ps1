$ErrorActionPreference = 'Stop'
Set-Location 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Computers\Prusa\Prusa-Firmware-Buddy'
$ts = '20260422_153045'
$log = "pa_dev\runs\stream_m573_$ts.log"
$out = "pa_dev\runs\stream_m573_$ts.stdout"
$err = "pa_dev\runs\stream_m573_$ts.stderr"
& 'C:\Python312\python.exe' -u 'pa_dev\tools\stream_gcode.py' 'COM5' 'pa_dev\preflights\m573_capture.gcode' '--log' $log '--quiet-rx' 1> $out 2> $err
Write-Output "EXITCODE=$LASTEXITCODE"
