; M573 smoke test (b)/(c) — filament preset already loaded by user
; M573 handles its own heating; caller owns entry position
; Nozzle left at Z=0.2 on exit — M114 confirms no Z-up in tail
G90               ; absolute XYZ
M83               ; relative E
G28               ; home (loadcell Z reference)
G1 Z5 F600        ; lift clear
G1 X10 Y5 F3000   ; move to purge zone
G1 Z0.2 F300      ; drop to first-layer height
M573              ; *** material-preset-aware dual-pulse capture ***
M114              ; report position — Z should still be 0.2
G1 Z5 F600        ; safe lift after test
G1 X100 Y100 F3000 ; park
