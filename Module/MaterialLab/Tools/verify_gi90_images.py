"""Independent PNG decoding and full displayed-image comparison for GI90."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image


def main():
    cli = argparse.ArgumentParser()
    cli.add_argument("prefix", type=Path)
    cli.add_argument("--output", required=True, type=Path)
    args = cli.parse_args()
    # Fixed before evaluation: permit sampling changes while bounding the
    # complete display, in addition to the linear-GI/contact/ghosting gates.
    limits = dict(mean_absolute=.025, p99_absolute=.08)
    pairs = {}
    manifests = []
    for path in sorted(args.prefix.parent.glob(args.prefix.name + "_gi90_*.png")):
        match = re.search(r"_v([01])_p(\d+)_f(\d+)_(total|indirect)\.png$", path.name)
        assert match, path
        view, profile, frame, stage = match.groups()
        with Image.open(path) as image:
            image.load()
            assert image.size == (1280, 800) and image.mode == "RGBA", (path, image.size, image.mode)
            data = np.asarray(image).copy()
        manifests.append(dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        if stage == "total":
            pairs.setdefault((view, frame), {})[profile] = data
    assert len(manifests) == 80 and len(pairs) == 20, "Missing quality checkpoint PNGs"
    rows = []
    for (view, frame), variants in sorted(pairs.items(), key=lambda x: (int(x[0][0]), int(x[0][1]))):
        assert len(variants) == 2 and "0" in variants
        profile = next(k for k in variants if k != "0")
        a, b = variants["0"], variants[profile]
        assert np.array_equal(a[:, :, 3], b[:, :, 3]), "Display alpha changed"
        difference = np.abs(a[:, :, :3].astype(np.float32) - b[:, :, :3]) / 255
        row = dict(view=int(view), frame=int(frame), profile=int(profile),
                   mean_absolute=float(difference.mean()), p99_absolute=float(np.quantile(difference, .99)),
                   maximum=float(difference.max()))
        row["accepted"] = row["mean_absolute"] <= limits["mean_absolute"] and row["p99_absolute"] <= limits["p99_absolute"]
        rows.append(row)
    result = dict(accepted=all(r["accepted"] for r in rows), limits=limits, pairs=rows, images=manifests,
                  scope="Presented frame's post-process texture; excludes window overlays and scanout")
    args.output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps(dict(accepted=result["accepted"], pngs=len(manifests), pairs=len(rows),
                          maximum_mae=max(r["mean_absolute"] for r in rows),
                          maximum_p99=max(r["p99_absolute"] for r in rows))))
    if not result["accepted"]:
        raise SystemExit("Displayed-image quality limits exceeded")


if __name__ == "__main__":
    main()
