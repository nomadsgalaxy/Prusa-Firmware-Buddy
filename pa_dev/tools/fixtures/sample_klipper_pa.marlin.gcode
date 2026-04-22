; Translated from Klipper to Marlin by klipper_to_marlin.py
; Minimal Klipper-style PA test snippet.
; Covers every translation path klipper_to_marlin.py handles.

; PRINT_START macro: replace with your Prusa start sequence
; was: PRINT_START BED=60 EXTRUDER=215
G28  ; was: G32
G29  ; was: BED_MESH_CALIBRATE
; BED_MESH_PROFILE not translated: BED_MESH_PROFILE LOAD=default

M203 X300 Y300  ; was: SET_VELOCITY_LIMIT VELOCITY=300 ACCEL=3000 SQUARE_CORNER_VELOCITY=5
M204 P3000 R3000 T3000  ; was: SET_VELOCITY_LIMIT VELOCITY=300 ACCEL=3000 SQUARE_CORNER_VELOCITY=5
; dropped (no Marlin equivalent): SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=5

; --- PA sweep ---
M900 K0.0000  ; was: SET_PRESSURE_ADVANCE ADVANCE=0.000 SMOOTH_TIME=0.040
G1 X10 Y20 Z0.2 F3000
G1 X40 Y20 E1.2 F600
M900 K0.0150  ; was: SET_PRESSURE_ADVANCE ADVANCE=0.015
G1 X10 Y22 F3000
G1 X40 Y22 E1.2 F600
M900 K0.0300  ; was: SET_PRESSURE_ADVANCE ADVANCE=0.030

; Some unrecognised Klipper commands we expect to warn on.
; WARN_UNTRANSLATED RESPOND MSG="hello"
; WARN_UNTRANSLATED SAVE_VARIABLE VARIABLE=x VALUE=1

; PRINT_END macro: replace with your Prusa end sequence
; was: PRINT_END
M900 K0.0000  ; restore linear-advance K-factor
