#!/usr/bin/env python3
"""Signature Oak icon parity test.

Signature Oak is a limited-edition Core One variant with a brass UI theme.
See doc/BFW-8113-signature-oak/design.md for details.

Brass icons in png_brass/ are OVERRIDES, not full copies. Only standard icons
that contain orange pixels need a brass counterpart (where orange is replaced
with brass). Icons without orange are used as-is for both variants.

When this test fails, it means a new icon with orange was added to png/
but no brass version exists in png_brass/. Contact the designer and request
a brass variant for the listed icon(s).
"""

import sys
from pathlib import Path

from PIL import Image

# Prusa orange: RGB(248, 101, 27)
ORANGE_RGB = (248, 101, 27)
COLOR_TOLERANCE = 30

# Icons temporarily waiting for a brass variant from the designer.
# Add filenames here to unblock CI while the asset is being prepared.
# Remove entries as soon as the brass icon is delivered.
PENDING_BRASS: set[str] = {
    # "example_icon_64x64.png",
}


def is_orange(r: int, g: int, b: int) -> bool:
    return (abs(r - ORANGE_RGB[0]) < COLOR_TOLERANCE
            and abs(g - ORANGE_RGB[1]) < COLOR_TOLERANCE
            and abs(b - ORANGE_RGB[2]) < COLOR_TOLERANCE)


def png_has_orange(path: Path) -> bool:
    img = Image.open(path).convert("RGBA")
    for r, g, b, a in img.getdata():
        if a < 10:
            continue
        if is_orange(r, g, b):
            return True
    return False


def main() -> int:
    source_dir = Path(sys.argv[1]) if len(
        sys.argv) > 1 else Path(__file__).parents[5]
    png_dir = source_dir / "src" / "gui" / "res" / "png"
    brass_dir = source_dir / "src" / "gui" / "res" / "png_brass"

    assert png_dir.is_dir(), f"PNG directory not found: {png_dir}"
    assert brass_dir.is_dir(), f"Brass directory not found: {brass_dir}"

    standard_icons = sorted(p.name for p in png_dir.glob("*.png"))
    brass_icons = set(p.name for p in brass_dir.glob("*.png"))
    assert standard_icons, "No PNG icons found"

    missing = [
        icon for icon in standard_icons
        if png_has_orange(png_dir / icon) and icon not in brass_icons
    ]

    pending = [icon for icon in missing if icon in PENDING_BRASS]
    failing = [icon for icon in missing if icon not in PENDING_BRASS]

    if pending:
        print(
            f"NOTE: {len(pending)} icon(s) pending brass variant (whitelisted):"
        )
        for icon in pending:
            print(f"  - {icon}")

    if failing:
        print(f"FAIL: {len(failing)} icon(s) with orange pixels but no brass "
              f"override in png_brass/.\n"
              f"Contact the designer and request a brass variant for:")
        for icon in failing:
            print(f"  - {icon}")
        return 1

    print(
        f"OK: checked {len(standard_icons)} icons, all orange icons have brass overrides"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
