; M573 3-pulse loadcell capture — standalone.
;   1. Heat + home + position at the purge-line start (X=10 Y=5 Z=0.2)
;   2. Run M573 (3 pulses: 1x high-flow purge + 2x low-flow measurement)
;   3. Lift, park, cool down
;
; Expected wall-clock: ~2 min heating + 4.2 s capture + dump
M155 S2                ; enable temp reporting
M104 S215              ; set hotend target
G90                    ; absolute XYZ
M83                    ; relative E
G28                    ; home (loadcell-based Z reference)
M109 S215              ; wait for hotend
G1 Z5 F600             ; lift clear of bed
G1 X10 Y5 F3000        ; move to capture start
G1 Z0.2 F300           ; drop to first-layer height
M573                   ; *** 3-pulse capture ***
G1 Z5 F600             ; lift clear
G1 X100 Y100 F3000     ; park over centre of bed
M104 S0                ; cool hotend
M155 S0                ; disable temp reporting
