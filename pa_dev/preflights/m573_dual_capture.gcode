; M573 dual-pulse loadcell capture
;   1. Heat to 220 C + home + position at purge-zone start (X=10 Y=5 Z=0.2)
;   2. Run M573 (dual-pulse: purge1 + pulse1 free-air + purge2 + pulse2 on-bed)
;   3. Lift, park, cool down
M155 S2                ; enable temp reporting
M104 S220              ; set hotend target
G90                    ; absolute XYZ
M83                    ; relative E
G28                    ; home (loadcell Z reference)
M109 S220              ; wait for hotend
G1 Z5 F600             ; lift clear of bed
G1 X10 Y5 F3000        ; move to capture start
G1 Z0.2 F300           ; drop to first-layer height
M573                   ; *** dual-pulse capture ***
G1 Z5 F600             ; lift clear
G1 X100 Y100 F3000     ; park
M104 S0                ; cool hotend
M155 S0                ; disable temp reporting
