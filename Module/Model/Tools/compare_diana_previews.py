"""Compare fixed-camera Model preview captures without scoring game fidelity.

The default rectangles describe the 1600x1000 Diana face preview. Region
coordinates are scaled for captures of the same layout at another resolution.
Inspector/timing text is deliberately excluded from the image measurements.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


REGIONS = {
    "viewport": (317, 76, 1202, 819),
    "crown": (510, 170, 1020, 430),
    "left_hair": (447, 385, 598, 735),
    "right_hair": (915, 385, 1070, 735),
    "left_eye": (604, 477, 696, 534),
    "right_eye": (775, 477, 872, 534),
    "cheek_control": (774, 570, 830, 624),
    "jacket_control": (341, 729, 429, 795),
}


def compare(first, second, output, labels):
    left, right = Image.open(first).convert("RGB"), Image.open(second).convert("RGB")
    if left.size != right.size:
        raise ValueError("Comparison images must have identical dimensions and camera/layout")
    a, b = np.asarray(left, dtype=np.float64) / 255, np.asarray(right, dtype=np.float64) / 255
    difference = np.abs(a - b)
    rectangles = {
        name: tuple(round(value * left.size[i % 2] / (1600, 1000)[i % 2])
                    for i, value in enumerate(rect))
        for name, rect in REGIONS.items()
    }
    report = {"first": str(first.resolve()), "second": str(second.resolve()),
              "size": list(left.size), "metric": "absolute display RGB difference, normalized 0..1",
              "interpretation": "Effect localization only; this does not measure similarity to the game.",
              "regions": {}}
    for name, (x0, y0, x1, y1) in rectangles.items():
        region = difference[y0:y1, x0:x1]
        report["regions"][name] = {
            "rectangle": [x0, y0, x1, y1], "meanAbsoluteRgb": float(region.mean()),
            "maximumAbsoluteRgb": float(region.max()),
            "changedPixels": int(np.any(region > .5 / 255, axis=2).sum()),
            "totalPixels": int(region.shape[0] * region.shape[1]),
        }
    viewport = rectangles["viewport"]
    crop_left, crop_right = left.crop(viewport), right.crop(viewport)
    # Difference is explicitly amplified for inspection, never used as a result.
    delta = Image.fromarray(np.uint8(np.clip(difference * 6, 0, 1) * 255)).crop(viewport)
    width, height = crop_left.size
    sheet = Image.new("RGB", (width * 3, height + 32), (24, 27, 33))
    draw = ImageDraw.Draw(sheet)
    for index, (picture, label) in enumerate(zip((crop_left, crop_right, delta), (*labels, "Absolute difference x6"))):
        sheet.paste(picture, (index * width, 32))
        draw.text((index * width + 12, 9), label, fill=(235, 238, 245))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output.with_suffix(".png"))
    output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["regions"], indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("output", type=Path, help="Output stem for PNG and JSON")
    parser.add_argument("--labels", nargs=2, default=("Baseline", "Enabled"))
    args = parser.parse_args()
    compare(args.first, args.second, args.output, args.labels)


if __name__ == "__main__":
    main()
