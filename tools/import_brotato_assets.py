#!/usr/bin/env python3
"""Import the Brotato playable slices from the supplied Unity project.

Requires Pillow. Example:
    python tools/import_brotato_assets.py --source "C:/path/to/Unity2021_Botato"

The Unity project is read only. Sprite .asset files in this recovered project
contain the rectangle and texture GUID; the texture .meta resolves the PNG.
Whole textures are retained only where the scene explicitly uses a single-sprite
PNG. No Unity installation or generated artwork is required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image
from brotato_scene import read_scene, write_catalog_headers


# All names are grounded in the supplied source; the player is the selectable
# Well-Rounded icon used by CharacterSetter rather than the blank potato mask.
SPRITES = {
    "player": ("well_rounded_icon", "CharacterSetter selectable Well-Rounded body"),
    "enemy": ("40001", "Assets/AI/NpcGo.prefab base enemy"),
    "weapon": ("wand", "Brotato.unity w1 / Weaponwand"),
    "bullet": ("bullet_wand", "w1 bullet_wand - Copy.prefab projectile"),
    "material": ("harvesting_icon", "Addition.prefab default collectible sprite"),
    "floor": ("tiles_1_0", "First map ground tile"),
    "spawn": ("entity_birth", "NpcGo.prefab spawn telegraph"),
    "weapon_torch": ("torch", "Brotato.unity w2 / Weapontorch; melee box"),
    "weapon_laser": ("taser", "Brotato.unity w3 / WeaponGun; six laser segments"),
    "weapon_knife": ("lightning_shiv", "Brotato.unity w4 / shiv; melee box"),
    "weapon_gun": ("smg", "Brotato.unity w5 / WeaponGun; BulletA projectile"),
    "weapon_burst": ("double_barrel_shotgun", "Brotato.unity w6 / WeaponGun; SpawnMove four-pellet animation"),
}

# These prefabs reference TextureImporter sub-sprites directly, rather than the
# recovered Sprite/*.asset objects. Preserve those exact rectangles and file IDs.
PROJECTILE_ATLAS_GUID = "685116be2efeee943b70a0ca1715215a"
TEXTURE_SPRITES = {
    "projectile_gun": (-1909389404, "Assets/PrefabInstance/BulletA.prefab SpriteRenderer"),
    "projectile_burst": (-1909389404, "Assets/PrefabInstance/SpawnMove.prefab four BulletB SpriteRenderers"),
    "laser_segment": (1101925732, "Brotato.unity w3 six nested Laser SpriteRenderers"),
    "muzzle_flash": (904847731, "Brotato.unity w5 and w6 Shoot / BulletAnim"),
}


def texture_sprite_data(path: Path, file_id: int) -> dict:
    text = path.read_text(encoding="utf-8-sig")
    entries = re.split(r"(?=^    - serializedVersion: \d+\n      name: )", text, flags=re.M)
    matches = [entry for entry in entries
               if re.search(rf"^      internalID: {file_id}$", entry, re.M)]
    if len(matches) != 1:
        raise ValueError(f"Expected one texture sub-sprite {file_id} in {path}")
    entry = matches[0]
    rect = re.search(
        r"      rect:\s+serializedVersion: \d+\s+x: ([\d.eE+-]+)\s+"
        r"y: ([\d.eE+-]+)\s+width: ([\d.eE+-]+)\s+height: ([\d.eE+-]+)", entry
    )
    ppu = re.search(r"^  spritePixelsToUnits: ([\d.eE+-]+)$", text, re.M)
    # Unity alignment 0 means Center; the serialized pivot is ignored for it.
    alignment = re.search(r"^      alignment: (\d+)$", entry, re.M)
    if not rect or not ppu or not alignment or alignment.group(1) != "0":
        raise ValueError(f"Unsupported texture sub-sprite metadata: {path}#{file_id}")
    return {
        "unity_rect_xywh": [float(value) for value in rect.groups()],
        "texture_guid": read_guid(path),
        "unity_pivot_xy": [0.5, 0.5],
        "pixels_per_unit": float(ppu.group(1)),
        "source_sprite_file_id": file_id,
        "source_sprite_kind": "TextureImporter sub-sprite",
    }


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_guid(path: Path) -> str:
    match = re.search(r"^guid: ([a-f0-9]{32})$", path.read_text(encoding="utf-8-sig"), re.M)
    if not match:
        raise ValueError(f"Missing GUID in {path}")
    return match.group(1)


def sprite_data(path: Path) -> dict:
    text = path.read_text(encoding="utf-8-sig")
    rect = re.search(
        r"  m_Rect:\s+serializedVersion: \d+\s+x: ([\d.eE+-]+)\s+"
        r"y: ([\d.eE+-]+)\s+width: ([\d.eE+-]+)\s+height: ([\d.eE+-]+)", text
    )
    texture = re.search(r"    texture: \{fileID: \d+, guid: ([a-f0-9]{32}), type: \d+\}", text)
    pivot = re.search(r"  m_Pivot: \{x: ([\d.eE+-]+), y: ([\d.eE+-]+)\}", text)
    ppu = re.search(r"  m_PixelsToUnits: ([\d.eE+-]+)", text)
    if not all((rect, texture, pivot, ppu)):
        raise ValueError(f"Unsupported Unity sprite metadata: {path}")
    return {
        "unity_rect_xywh": [float(value) for value in rect.groups()],
        "texture_guid": texture.group(1),
        "unity_pivot_xy": [float(value) for value in pivot.groups()],
        "pixels_per_unit": float(ppu.group(1)),
    }


def import_assets(source: Path, output: Path, catalog_output: Path | None = None) -> dict:
    source = source.resolve(strict=True)
    output = output.resolve()
    if output == source or source in output.parents:
        raise ValueError("Output must be outside the read-only Unity source project")
    assets = source / "Assets"
    if not (assets / "Brotato.unity").is_file():
        raise ValueError("--source must name the Unity project containing Assets/Brotato.unity")
    scene = read_scene(source)
    if catalog_output is not None:
        catalog_output = catalog_output.resolve()
        if catalog_output == source or source in catalog_output.parents:
            raise ValueError("Generated headers must be outside the read-only Unity source project")

    # Restrict GUID discovery to source PNG textures, not plugin/examples/assets.
    textures = {
        read_guid(meta): meta.with_suffix("")
        for meta in sorted((assets / "Texture2D").glob("*.png.meta"))
    }
    records = []
    prepared_images = []
    sources = []
    for name, (sprite_name, purpose) in SPRITES.items():
        sprite = assets / "Sprite" / f"{sprite_name}.asset"
        sprite_meta = sprite.with_suffix(".asset.meta")
        data = sprite_data(sprite)
        sources.append((name, purpose, sprite, sprite_meta, data))
    atlas = textures.get(PROJECTILE_ATLAS_GUID)
    if atlas is None:
        raise ValueError(f"Missing phase-two projectile atlas {PROJECTILE_ATLAS_GUID}")
    for name, (file_id, purpose) in TEXTURE_SPRITES.items():
        sprite_meta = atlas.with_suffix(".png.meta")
        sources.append((name, purpose, sprite_meta, sprite_meta,
                        texture_sprite_data(sprite_meta, file_id)))
    existing_names = {entry[0] for entry in sources}
    for entry in scene["images"]:
        if entry["id"] in existing_names:
            continue
        sources.append((entry["id"], entry["purpose"], source / entry["source_sprite"],
                        source / entry["source_meta"], entry["data"]))
    for name, purpose, sprite, sprite_meta, data in sources:
        texture = textures.get(data["texture_guid"])
        if texture is None:
            raise ValueError(f"Unresolved texture {data['texture_guid']} for {sprite}")
        with Image.open(texture) as atlas:
            image = atlas.convert("RGBA")
        x, y, width, height = data["unity_rect_xywh"]
        # Unity rect origin is bottom left; Pillow uses top left. Outward rounding
        # preserves the edge pixels in recovered fractional tight-sprite rects.
        box = (math.floor(x), image.height - math.ceil(y + height),
               math.ceil(x + width), image.height - math.floor(y))
        if not (0 <= box[0] < box[2] <= image.width and 0 <= box[1] < box[3] <= image.height):
            raise ValueError(f"Sprite rectangle lies outside its texture: {sprite}")
        cropped = image.crop(box)
        if not cropped.getchannel("A").getbbox():
            raise ValueError(f"Sprite crop is fully transparent: {sprite}")
        prepared_images.append((name, cropped))
        records.append({
            "id": name,
            "output": f"{name}.png",
            "purpose": purpose,
            "source_sprite": sprite.relative_to(source).as_posix(),
            "source_sprite_guid": read_guid(sprite_meta),
            "source_sprite_sha256": sha256(sprite),
            "source_texture": texture.relative_to(source).as_posix(),
            "source_texture_sha256": sha256(texture),
            **data,
            "crop_top_left_ltrb": list(box),
            "output_size_px": list(cropped.size),
            "unity_size_units": [width / data["pixels_per_unit"], height / data["pixels_per_unit"]],
        })

    # Validate every source first so invalid input cannot leave a partial import.
    output.mkdir(parents=True, exist_ok=True)
    for (name, image), record in zip(prepared_images, records):
        destination = output / f"{name}.png"
        image.save(destination, format="PNG", optimize=False)
        record["output_sha256"] = sha256(destination)
    manifest = {
        "format_version": 1,
        "source_project": "Unity2021_Botato",
        "source_scene": "Assets/Brotato.unity",
        "source_scene_sha256": sha256(assets / "Brotato.unity"),
        "scope": "Playable slices 1-3: five characters, five authored maps, enemy, six source weapons, projectiles, laser, muzzle flash, collectible, ground, spawn marker",
        "operation": "RGBA crop of original art; no resampling or recoloring",
        "rect_convention": "Unity bottom-left xywh; output crop is Pillow top-left ltrb; outward rounding",
        "assets": records,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (output / "scene_manifest.json").write_text(json.dumps(scene, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    if catalog_output is not None:
        write_catalog_headers(scene, catalog_output)
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, required=True, help="Read-only Unity2021_Botato project directory")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "Asset" / "Brotato")
    parser.add_argument("--catalog-output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "Module" / "Brotato" / "Public",
                        help="Generated content_catalog.h and map_layout_data.h destination")
    args = parser.parse_args()
    manifest = import_assets(args.source, args.output, args.catalog_output)
    print(f"Imported {len(manifest['assets'])} source sprites to {args.output.resolve()}")
    for asset in manifest["assets"]:
        print(f"  {asset['output']}: {asset['output_size_px'][0]}x{asset['output_size_px'][1]} ({asset['source_sprite']})")


if __name__ == "__main__":
    main()
