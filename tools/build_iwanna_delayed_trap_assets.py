"""Bake the user-provided laser sheet and explosion package into portable assets.

Run: python tools/build_iwanna_delayed_trap_assets.py --project D:/Games/MyIwana
Requires Pillow and an existing ffmpeg executable only while preparing assets.
The game uses ordinary transparent PNG atlases and PCM WAV; no Spine or MP3
runtime/dependency is introduced. Originals are read-only. The explosion bake
composes the supplied PNG sequences; it does not execute the binary Spine file.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import shutil
import stat
import subprocess
import wave
import zipfile
from pathlib import Path, PurePosixPath

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[1]
# Reviewed against the 1024 x 1024 source. Each rectangle contains one blue
# lightning beam. The four lower frames are the second phase of the four upper
# frames. Bottom edges deliberately exclude the adjacent sprite's first row.
LASER_BOXES = [
    (262, 257, 385, 557), (385, 208, 492, 507),
    (493, 205, 592, 507), (592, 205, 674, 496),
    (262, 557, 382, 853), (382, 557, 488, 854),
    (491, 507, 591, 799), (591, 498, 674, 799),
]
LASER_ORDER = [0, 4, 1, 5, 2, 6, 3, 7]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract_package(source: Path, destination: Path) -> dict[str, Image.Image]:
    """Validate every archive member before writing any file (no path escapes)."""
    destination = destination.resolve()
    images: dict[str, Image.Image] = {}
    with zipfile.ZipFile(source) as archive:
        members = archive.infolist()
        if len(members) > 512 or sum(x.file_size for x in members) > 64 * 1024 * 1024:
            raise ValueError("Explosion package exceeds the preparation size limit")
        for member in members:
            relative = PurePosixPath(member.filename.replace("\\", "/"))
            if relative.is_absolute() or ".." in relative.parts or any(":" in p for p in relative.parts):
                raise ValueError(f"Unsafe archive member: {member.filename}")
            if stat.S_ISLNK(member.external_attr >> 16):
                raise ValueError(f"Archive links are not supported: {member.filename}")
            target = (destination / Path(*relative.parts)).resolve()
            if destination != target and destination not in target.parents:
                raise ValueError(f"Archive path escapes destination: {member.filename}")
        for member in members:
            target = destination / Path(*PurePosixPath(member.filename.replace("\\", "/")).parts)
            if member.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            data = archive.read(member)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            if target.suffix.lower() == ".png":
                if target.name in images:
                    raise ValueError(f"Duplicate image basename in explosion package: {target.name}")
                images[target.name] = Image.open(io.BytesIO(data)).convert("RGBA")
    return images


def place(canvas: Image.Image, source: Image.Image, center: tuple[int, int],
          size: int, opacity: float = 1.0) -> None:
    ratio = size / max(source.size)
    sprite = source.resize((max(1, round(source.width * ratio)),
                            max(1, round(source.height * ratio))), Image.Resampling.LANCZOS)
    if opacity < 1:
        sprite.putalpha(sprite.getchannel("A").point(lambda x: round(x * max(0, opacity))))
    canvas.alpha_composite(sprite, (center[0] - sprite.width // 2,
                                    center[1] - sprite.height // 2))


def explosion_frames(images: dict[str, Image.Image]) -> list[Image.Image]:
    """One 24-frame flash -> fire -> smoke bake with a stable world-space pivot."""
    result = []
    for frame in range(24):
        canvas = Image.new("RGBA", (192, 192))
        # Smoke follows the initial blast and dissipates. All frames retain the
        # same 192px canvas; trimmed source extents never move the gameplay pivot.
        if frame >= 6:
            smoke = min(18, round((frame - 6) * 18 / 17))
            place(canvas, images[f"smog2_{smoke:04d}.png"], (96, 92),
                  round(117 + (frame - 6) * 2.1), min(.68, (24 - frame) / 8))
        if 1 <= frame <= 16:
            fire = min(6, (frame - 1) // 2)
            place(canvas, images[f"fire_smog2_{fire:04d}.png"], (96, 96),
                  round(111 + frame * 2.4), min(1.0, (17 - frame) / 3))
        if frame < 4:
            place(canvas, images["explode_areal.png"], (96, 96),
                  [90, 174, 160, 146][frame], [1.0, 1.0, .72, .3][frame])
        if frame in (2, 3, 4, 5):
            source = "sound_wave_0001.png" if frame < 4 else "sound_wave_0002.png"
            place(canvas, images[source], (96, 96), 125 + frame * 9, .5)
        result.append(canvas)
    return result


def laser_frames(source: Path) -> list[Image.Image]:
    sheet = Image.open(source).convert("RGBA")
    if sheet.size != (1024, 1024):
        raise ValueError("Lasers.png changed dimensions; review the crop rectangles before rebuilding")
    frames = []
    for index in LASER_ORDER:
        sprite = sheet.crop(LASER_BOXES[index])
        canvas = Image.new("RGBA", (128, 320))
        # Align the beam cap at y=310 so changing source trim never jitters the
        # emitter. The source is not rescaled, recolored or made opaque.
        canvas.alpha_composite(sprite, ((128 - sprite.width) // 2, 310 - sprite.height))
        frames.append(canvas)
    return frames


def pack(frames: list[Image.Image], columns: int) -> Image.Image:
    width, height = frames[0].size
    rows = math.ceil(len(frames) / columns)
    atlas = Image.new("RGBA", (columns * width, rows * height))
    for index, frame in enumerate(frames):
        if frame.size != (width, height):
            raise ValueError("Animation frames need a consistent canvas")
        atlas.alpha_composite(frame, ((index % columns) * width, (index // columns) * height))
    return atlas


def preview(frames: list[Image.Image], columns: int, output: Path) -> None:
    width, height = frames[0].size
    cell_height = height + 20
    image = Image.new("RGBA", (columns * width, math.ceil(len(frames) / columns) * cell_height),
                      (29, 34, 42, 255))
    draw = ImageDraw.Draw(image)
    for index, frame in enumerate(frames):
        x, y = (index % columns) * width, (index // columns) * cell_height
        image.alpha_composite(frame, (x, y))
        draw.text((x + 5, y + height + 2), str(index), fill=(225, 228, 237, 255))
    image.convert("RGB").save(output)


def pcm_wav(ffmpeg: str, source: Path, output: Path) -> dict:
    subprocess.run([ffmpeg, "-v", "error", "-nostdin", "-y", "-i", str(source),
                    "-map_metadata", "-1", "-vn", "-ac", "1", "-ar", "44100",
                    "-c:a", "pcm_s16le", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                    str(output)], check=True)
    with wave.open(str(output), "rb") as sound:
        if sound.getsampwidth() != 2 or sound.getnchannels() != 1 or sound.getcomptype() != "NONE":
            raise ValueError(f"Expected PCM16 mono WAV: {output}")
        return {"file": output.name, "format": "PCM16", "sampleRate": sound.getframerate(),
                "channels": sound.getnchannels(), "duration": sound.getnframes() / sound.getframerate(),
                "sha256": digest(output)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True, help="Project containing the supplied source assets")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"), help="Already installed ffmpeg executable")
    parser.add_argument("--output", type=Path, default=REPO / "Asset/IWanna", help="Runtime asset output root")
    parser.add_argument("--work", type=Path, default=REPO / "build/delayed_trap_assets", help="Extraction and contact sheets")
    args = parser.parse_args()
    if not args.ffmpeg:
        parser.error("No existing ffmpeg executable found; supply --ffmpeg (the game itself needs no ffmpeg)")
    source = args.project.resolve()
    output = args.output.resolve()
    work = args.work.resolve()
    paths = {"laserSheet": source / "images/Lasers.png", "explosionArchive": source / "images/爆炸特效.zip",
             "laserSound": source / "audio/Laser.wav", "explosionSound": source / "audio/Boom.mp3"}
    for path in paths.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    for folder in (output / "images", output / "audio", work):
        folder.mkdir(parents=True, exist_ok=True)
    extracted = extract_package(paths["explosionArchive"], work / "extracted")
    animations = {
        "explosion": {"image": "trap_explosion_atlas.png", "columns": 6, "rows": 4, "frames": 24,
                      "cellWidth": 192, "cellHeight": 192, "duration": 1.0, "loop": False,
                      "framesPerSecond": 24, "pivot": [0.5, 0.5]},
        "laser": {"image": "trap_laser_atlas.png", "columns": 8, "rows": 1, "frames": 8,
                  "cellWidth": 128, "cellHeight": 320, "duration": 0.4, "loop": True,
                  "framesPerSecond": 20, "pivot": [0.5, 0.5],
                  "sourceRectangles": LASER_BOXES, "frameOrder": LASER_ORDER},
    }
    for key, frames in (("explosion", explosion_frames(extracted)), ("laser", laser_frames(paths["laserSheet"]))):
        data = animations[key]
        target = output / "images" / data["image"]
        pack(frames, data["columns"]).save(target)
        data["sha256"] = digest(target)
        preview(frames, data["columns"], work / f"{key}_contact.jpg")
    audio = {key: pcm_wav(args.ffmpeg, paths[source_key], output / "audio" / filename)
             for key, source_key, filename in (("trap_laser", "laserSound", "trap_laser.wav"),
                                               ("trap_explosion", "explosionSound", "trap_explosion.wav"))}
    metadata = {"format": "IWANNA_TRAP_ASSETS_1", "generator": "tools/build_iwanna_delayed_trap_assets.py",
                "sources": {key: {"file": path.name, "sha256": digest(path)} for key, path in paths.items()},
                "explosionBake": "PNG layers composed offline; original Spine project is preserved in the extraction folder.",
                "animations": animations, "audio": audio}
    manifest = output / "images/trap_animations.json"
    manifest.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if source != output:
        for relative in [Path("images") / v["image"] for v in animations.values()] + [
                Path("audio") / v["file"] for v in audio.values()] + [Path("images/trap_animations.json")]:
            target = source / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(output / relative, target)
    print(json.dumps(metadata, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
