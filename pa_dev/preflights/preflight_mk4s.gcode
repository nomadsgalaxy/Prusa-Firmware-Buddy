# Pre-flight for M573 on MK4S.
# Send before the capture run to heat + home + position the printer.
# Final M109 blocks until hotend reaches target.
M155 S2
M104 S215
G28
G1 Z5 F600
G1 X10 Y5 F3000
M109 S215
