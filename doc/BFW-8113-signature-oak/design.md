# BFW-8113: Signature Oak

## What is Signature Oak?

Signature Oak is a limited-edition Core One with an oak chassis. The firmware adds visual branding — brass-toned icons and a brass accent color — to match the hardware aesthetic. Functionally, Oak behaves identically to a standard Core One.

## What changes for the user?

- **Home screen** shows "SIGNATURE OAK" instead of "PRUSA COREONE"
- **Icons** throughout the UI use brass tones instead of Prusa orange
- **Accent color** (`COLOR_BRAND`) is brass instead of orange
- **G-code compatibility** is full: Oak prints Core One sliced files and vice versa
- **OTA updates** are routed separately — Oak gets Oak firmware, standard gets standard

## How does Oak identify itself?

Oak is a separate build variant, not a runtime setting. It produces a distinct firmware binary with its own printer model (`coreone_oak`). To the bootloader, Oak identifies as `7.1.0` (same as standard Core One) so existing bootloaders accept it. To Connect, it identifies as `7.2.0` so OTA updates are routed to the correct variant.

## Design decisions

### Build variant vs runtime detection

Considered detecting Oak by serial number or a persistent setting, but:
- Icon resources are baked into flash at build time (QOI-packed). No mechanism exists to swap icon sets at runtime.
- `COLOR_BRAND` is `constexpr` throughout the GUI (~50 call sites). Making it runtime would require significant refactoring.
- Connect needs separate firmware routing, which a distinct device type gives for free.

A runtime approach would be cleaner architecturally, but the effort is disproportionate for a limited edition.

### Separate device type `7.2.0`

If both variants shared `7.1.0`, Connect would deliver whichever firmware was uploaded last to all Core One printers. Separate device type means automatic OTA routing. This follows the MK4 family pattern (`1.4.0`/`1.4.1`/`1.3.9`/`1.3.10`).

Oak keeps `PRINTER_VERSION=1` at build level because bootloader 2.5.0 only accepts `7.1.0` for Core One hardware. The `7.2.0` identity comes from the `PrinterModelInfo` table, which Connect reads.

### Separate `PrinterModel::coreone_oak` enum

Initially considered `#if SIGNATURE_OAK()` conditionals to switch fields within a single printer model entry. A separate enum value is cleaner: both models exist in the table unconditionally, tools can discover both variants, and the code is easier to reason about.

### G-code cross-compatibility

Both `coreone` and `coreone_oak` share `PrinterModelCompatibilityGroup::coreone`, so g-code checks pass in either direction. Oak has its own check code (`380`, from USB PID `0x26`=38) but compatibility is resolved at the group level.

### Brass icon overlay

Only icons that differ from standard are kept in `png_brass/` (171 of 256). CMake copies standard icons first, then brass on top — only matching filenames get replaced. A unit test scans standard icons for orange pixels and fails if a brass override is missing, so new icons added upstream won't silently ship with the wrong color.

### MMU

Not expected on Oak (visual conflict with luxury aesthetic), but included since Oak shares `PRINTER=COREONE` which enables `HAS_MMU2`. MMU requires user opt-in via menu.

## Blocking dependencies

- **Connect backend**: Must add `CoreOneOak` device type `7.2.0` before first Oak release, otherwise OTA won't work.
- **Slicer** (SPE-3318): COREONEOAK profiles scheduled Feb 2026. Until then, Oak uses standard Core One profiles.
