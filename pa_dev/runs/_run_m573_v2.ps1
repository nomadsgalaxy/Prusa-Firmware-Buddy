$ErrorActionPreference = 'Stop'
$root = 'C:\Users\Antdr\OneDrive\Computers\Work Computers\Computers\Prusa\Prusa-Firmware-Buddy'
$ts = '20260422_153600'
$log = Join-Path $root "pa_dev\runs\stream_m573_$ts.log"
$out = Join-Path $root "pa_dev\runs\stream_m573_$ts.stdout"
$err = Join-Path $root "pa_dev\runs\stream_m573_$ts.stderr"
$py  = 'C:\Python312\python.exe'
$script = Join-Path $root 'pa_dev\tools\stream_gcode.py'
$gcode  = Join-Path $root 'pa_dev\preflights\m573_capture.gcode'

# Quick pre-flight sanity checks, logged to a separate probe file so we can see them from the sandbox.
$probe = Join-Path $root "pa_dev\runs\_launch_probe_$ts.txt"
"START $(Get-Date -Format o)"                              | Out-File -Encoding ascii $probe
"python exists: $(Test-Path $py)"                          | Out-File -Encoding ascii -Append $probe
"script exists: $(Test-Path $script)"                      | Out-File -Encoding ascii -Append $probe
"gcode  exists: $(Test-Path $gcode)"                       | Out-File -Encoding ascii -Append $probe
"root exists:   $(Test-Path $root)"                        | Out-File -Encoding ascii -Append $probe

try {
    $p = Start-Process -FilePath $py `
        -ArgumentList @('-u', $script, 'COM5', $gcode, '--log', $log, '--quiet-rx') `
        -WorkingDirectory $root `
        -RedirectStandardOutput $out `
        -RedirectStandardError $err `
        -PassThru -NoNewWindow
    "STREAM_PID=$($p.Id)" | Out-File -Encoding ascii -Append $probe
} catch {
    "LAUNCH_ERROR: $_" | Out-File -Encoding ascii -Append $probe
}
