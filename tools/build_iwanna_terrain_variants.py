"""Build the three seamless, edge-aware pixel-terrain families used by IWanna rooms.

This is an offline authoring tool. The game loads ordinary PNG files and has no
Python or Pillow dependency. A suffix of 00..15 encodes exposed sides using
top=1, right=2, bottom=4, left=8. ``_alt`` is a second stone pattern with the
same silhouette, so room authors may alternate the two without changing
collision or producing seams along joined cells.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import random

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1] / "Asset" / "IWanna"
OUT = ROOT / "images"
THEMES = {
    "moss": {
        "base": "#34434a", "stone": ("#455761", "#4e6067", "#3c4d55"),
        "light": "#80948e", "shade": "#22343c", "joint": "#2b3d45",
        "rim": "#99c478", "accent": "#599a59", "spark": "#b4da93",
    },
    "ember": {
        "base": "#533b39", "stone": ("#795146", "#895a4b", "#65463f"),
        "light": "#c18e69", "shade": "#352a32", "joint": "#493331",
        "rim": "#d9ae77", "accent": "#b8684d", "spark": "#efd4a0",
    },
    "azure": {
        "base": "#30475a", "stone": ("#426277", "#4b6b7e", "#354f68"),
        "light": "#8cacb3", "shade": "#1c3047", "joint": "#283c53",
        "rim": "#88d8dd", "accent": "#477f9a", "spark": "#c0f4ec",
    },
}


def tile(theme_name: str, edges: int, alternate: bool) -> Image.Image:
    p = THEMES[theme_name]
    seed = 101 + list(THEMES).index(theme_name) * 733 + (491 if alternate else 0)
    rng = random.Random(seed)
    image = Image.new("RGB", (32, 32), p["base"])
    draw = ImageDraw.Draw(image)

    # The two outermost pixel rings share one color across all variants. This
    # keeps adjacent filled tiles visually joined instead of outlining squares.
    for y in range(2, 30):
        for x in range(2, 30):
            noise = (x * 73856093 ^ y * 19349663 ^ seed * 83492791) & 31
            if noise in (0, 3, 11):
                draw.point((x, y), fill=p["stone"][noise % 3])

    # Uneven inset flagstones. They form larger clusters and short mortar
    # lines, not a lattice aligned with every tile cell.
    bands = [(-3, 6), (6, 16), (16, 25), (25, 35)]
    for row, (top, bottom) in enumerate(bands):
        split = (11 if row % 2 else 21) + (3 if alternate else 0)
        for left, right in ((-3, split), (split, 35)):
            wiggle = rng.randrange(-2, 3)
            polygon = [
                (max(2, left + 2), max(2, top + 2)),
                (min(29, right - 2), max(2, top + 2 + wiggle)),
                (min(29, right - 2), min(29, bottom - 2)),
                (max(2, left + 2), min(29, bottom - 2 - wiggle)),
            ]
            draw.polygon(polygon, fill=p["stone"][(row + int(alternate) + (left > 0)) % 3])
            if polygon[0][0] < polygon[1][0] and polygon[0][1] < 29:
                draw.line((polygon[0], polygon[1]), fill=p["light"])
                draw.point((polygon[0][0], min(29, polygon[0][1] + 1)), fill=p["spark"])
            if polygon[2][1] < 29:
                draw.line((polygon[3], polygon[2]), fill=p["joint"])

    # Hand-sized cracks and ore/lichen flecks keep broad walls from reading as
    # one repeated brick stamp. Their ends stay inside the cell.
    crack_x = 7 + rng.randrange(0, 17)
    crack_y = 9 + rng.randrange(0, 15)
    draw.line([(crack_x, crack_y), (crack_x + 2, crack_y + 2),
               (crack_x + 1, crack_y + 5), (crack_x + 4, crack_y + 7)],
              fill=p["shade"])
    draw.point((crack_x - 1, crack_y - 1), fill=p["light"])
    for i in range(5 if alternate else 3):
        x, y = rng.randrange(4, 28), rng.randrange(5, 28)
        draw.point((x, y), fill=p["accent"] if i % 2 else p["light"])

    if alternate:
        if theme_name == "moss":
            draw.polygon([(18, 21), (23, 18), (27, 21), (25, 25), (20, 26)], fill=p["accent"])
            draw.point((21, 21), fill=p["spark"])
        elif theme_name == "ember":
            draw.line([(7, 25), (12, 22), (18, 24), (22, 20)], fill=p["accent"], width=2)
            draw.point((18, 23), fill=p["spark"])
        else:
            draw.polygon([(8, 25), (10, 19), (13, 22), (16, 17), (17, 25)], fill=p["accent"])
            draw.line([(10, 19), (13, 22), (16, 17)], fill=p["spark"])

    # Draw only exposed faces. Top caps have an irregular profile; buried
    # sides are never beveled, so neighboring solid tiles stay continuous.
    if edges & 1:
        draw.rectangle((0, 0, 31, 1), fill=p["rim"])
        draw.line((0, 2, 31, 2), fill=p["accent"])
        for x in range(0, 32, 4):
            h = 3 + ((x * 5 + seed) % 4)
            draw.rectangle((x + 1, 3, min(31, x + 2), h), fill=p["accent"])
            if (x + seed) % 3 == 0:
                draw.point((min(31, x + 1), 0), fill=p["spark"])
    if edges & 2:
        draw.rectangle((30, 0, 31, 31), fill=p["shade"])
        draw.line((29, 3, 29, 29), fill=p["light"])
    if edges & 4:
        draw.rectangle((0, 30, 31, 31), fill=p["shade"])
        draw.line((3, 29, 28, 29), fill=p["joint"])
    if edges & 8:
        draw.rectangle((0, 0, 1, 31), fill=p["shade"])
        draw.line((2, 3, 2, 29), fill=p["light"])
    return image.resize((128, 128), Image.Resampling.NEAREST)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    checksums = {}
    for theme in THEMES:
        for alternate in (False, True):
            for edges in range(16):
                name = f"terrain_{theme}{'_alt' if alternate else ''}_{edges:02}.png"
                path = OUT / name
                tile(theme, edges, alternate).save(path)
                checksums[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {
        "author": "Original procedural pixel art generated for this project",
        "generator": "tools/build_iwanna_terrain_variants.py",
        "edgeBits": {"top": 1, "right": 2, "bottom": 4, "left": 8},
        "sha256": checksums,
    }
    (ROOT / "tile_theme_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(checksums)} edge-aware terrain tiles in {OUT}")


if __name__ == "__main__":
    main()
