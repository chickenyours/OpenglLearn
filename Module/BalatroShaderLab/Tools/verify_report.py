"""Check the offline report's local images and important rendering comparisons."""
from html.parser import HTMLParser
from pathlib import Path
import json
import sys
from urllib.parse import unquote, urlparse
from PIL import Image, ImageChops, ImageStat


class Images(HTMLParser):
    def __init__(self):
        super().__init__()
        self.images = []
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "img":
            source = dict(attrs).get("src", "")
            if source:  # The click-to-zoom dialog starts with an empty image.
                self.images.append(source)
        elif tag == "a":
            target = dict(attrs).get("href", "")
            if target:
                self.links.append(target)


report = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "Report" / "index.html"
parser = Images()
parser.feed(report.read_text(encoding="utf-8"))
results = {"report": str(report), "images": [], "comparisons": {}}
for target in parser.links:
    parsed = urlparse(target)
    if not parsed.scheme and parsed.path:
        path = report.parent / unquote(parsed.path)
        if not path.exists():
            raise FileNotFoundError(f"Broken local report link: {target}")
results["checked_local_links"] = sum(not urlparse(link).scheme and bool(urlparse(link).path) for link in parser.links)
for source in parser.images:
    if urlparse(source).scheme:
        raise ValueError(f"Report image should work offline: {source}")
    path = report.parent / unquote(source.split("#")[0])
    with Image.open(path) as image:
        image.load()
        std = sum(ImageStat.Stat(image.convert("RGB")).stddev) / 3
        if std < 1:
            raise ValueError(f"Blank image: {path}")
        results["images"].append({"path": source, "size": image.size, "mean_channel_stddev": round(std, 3)})


def difference(a, b, box=(40, 130, 1560, 820)):
    with Image.open(report.parent / "images" / a) as first, Image.open(report.parent / "images" / b) as second:
        # Omit annotation bands: compare the GPU scene itself.
        delta = ImageChops.difference(first.convert("RGB").crop(box), second.convert("RGB").crop(box))
        return sum(ImageStat.Stat(delta).mean) / 3


images = report.parent / "images"
if all((images / p).exists() for p in ("background-small.png", "background-big.png")):
    value = difference("background-small.png", "background-big.png")
    results["comparisons"]["small_big_scene_mean_abs_difference"] = value
    if value > 0.01:
        raise ValueError("Small/Big should share the same source formula and palette at fixed time")
if all((images / p).exists() for p in ("crt-before.png", "crt-after.png")):
    value = difference("crt-before.png", "crt-after.png")
    results["comparisons"]["crt_toggle_scene_mean_abs_difference"] = value
    if value < 0.1:
        raise ValueError("CRT toggle did not visibly change the output")
if all((images / p).exists() for p in ("card-editions.png", "card-editions-later.png")):
    # Compare within Holo's opaque card face, so moving background cannot pass.
    value = difference("card-editions.png", "card-editions-later.png", (720, 280, 900, 510))
    results["comparisons"]["holo_face_time_change_mean_abs_difference"] = value
    if value < 0.1:
        raise ValueError("Edition clock did not change the Holo card face")
if not results["images"]:
    raise ValueError("Report contains no images")
print(json.dumps(results, indent=2, ensure_ascii=False))
