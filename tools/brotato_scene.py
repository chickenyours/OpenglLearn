"""Read the recovered Brotato Unity scene without Unity or third-party YAML loaders.

The importer follows serialized sprite references, transforms, tint and pivots.
It deliberately preserves the five whole-texture decoration references present in
this source scene. The input project is read-only; generated outputs live here.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
from PIL import Image


_NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"


def _scalar(text: str, key: str) -> str:
    match = re.search(r"^  " + re.escape(key) + r": (.*)$", text, re.M)
    if not match:
        raise ValueError(f"Missing Unity property: {key}")
    return match[1]


def _vector(text: str, key: str) -> list[float]:
    return [float(n) for n in re.findall(r": (" + _NUMBER + r")", _scalar(text, key))]


def _guid(path: Path) -> str:
    return re.search(r"^guid: ([a-f0-9]{32})$", path.read_text(encoding="utf-8-sig"), re.M)[1]


def _sprite_data(asset: Path) -> dict:
    """Metadata for the scene's recovered Sprite assets or single-sprite PNGs."""
    if asset.suffix == ".png":
        text = asset.with_suffix(".png.meta").read_text(encoding="utf-8-sig")
        if not re.search(r"^  spriteMode: 1$", text, re.M):
            raise ValueError(f"Expected a single-sprite PNG: {asset}")
        with Image.open(asset) as image:
            width, height = image.size
        return dict(unity_rect_xywh=[0.0, 0.0, float(width), float(height)],
                    texture_guid=_guid(asset.with_suffix(".png.meta")),
                    unity_pivot_xy=_vector(text, "spritePivot"),
                    pixels_per_unit=float(_scalar(text, "spritePixelsToUnits")),
                    border_units=[float(n) / float(_scalar(text, "spritePixelsToUnits"))
                                  for n in _vector(text, "spriteBorder")])
    text = asset.read_text(encoding="utf-8-sig")
    rect = re.search(r"  m_Rect:\s+serializedVersion: \d+\s+x: (" + _NUMBER +
                     r")\s+y: (" + _NUMBER + r")\s+width: (" + _NUMBER +
                     r")\s+height: (" + _NUMBER + r")", text)
    texture = re.search(r"    texture: \{fileID: \d+, guid: ([a-f0-9]{32}), type: \d+\}", text)
    if not rect or not texture:
        raise ValueError(f"Unsupported recovered sprite: {asset}")
    ppu = float(_scalar(text, "m_PixelsToUnits"))
    return dict(unity_rect_xywh=[float(n) for n in rect.groups()],
                texture_guid=texture[1], unity_pivot_xy=_vector(text, "m_Pivot"),
                pixels_per_unit=ppu,
                border_units=[n / ppu for n in _vector(text, "m_Border")])


def _rotate(q: list[float], v: list[float]) -> list[float]:
    # Serialized Unity quaternions are rounded. Normalize before composition.
    norm = math.sqrt(sum(n * n for n in q))
    x, y, z, w = (n / norm for n in q)
    a, b, c = v
    return [(1 - 2*y*y - 2*z*z)*a + (2*x*y - 2*z*w)*b + (2*x*z + 2*y*w)*c,
            (2*x*y + 2*z*w)*a + (1 - 2*x*x - 2*z*z)*b + (2*y*z - 2*x*w)*c,
            (2*x*z - 2*y*w)*a + (2*y*z + 2*x*w)*b + (1 - 2*x*x - 2*y*y)*c]


def read_scene(source: Path) -> dict:
    assets = source / "Assets"
    scene_path = assets / "Brotato.unity"
    text = scene_path.read_text(encoding="utf-8-sig")
    matches = list(re.finditer(r"^--- !u!(\d+) &(\d+).*\n", text, re.M))
    blocks, types, lines = {}, {}, {}
    line = 1
    previous = 0
    for i, match in enumerate(matches):
        fid = match[2]
        blocks[fid] = text[match.end():matches[i+1].start() if i+1 < len(matches) else len(text)]
        types[fid] = match[1]
        line += text.count("\n", previous, match.start())
        previous = match.start()
        lines[fid] = line
    names = {fid: _scalar(s, "m_Name") for fid, s in blocks.items() if types[fid] == "1"}
    owner = {fid: match[1] for fid, s in blocks.items()
             if (match := re.search(r"^  m_GameObject: \{fileID: (\d+)\}", s, re.M))}
    components: dict[str, list[str]] = {}
    transforms = {}
    for fid, oid in owner.items():
        components.setdefault(oid, []).append(fid)
        if types[fid] in ("4", "224"):
            transforms[oid] = fid
    transform_owner = {fid: oid for oid, fid in transforms.items()}
    parent = {oid: transform_owner.get(re.search(r"m_Father: \{fileID: (\d+)\}", blocks[fid])[1], "")
              for oid, fid in transforms.items()}
    meta = {_guid(p): p.with_suffix("") for directory in (assets / "Sprite", assets / "Texture2D")
            for p in sorted(directory.glob("*.meta"))}
    sprite_cache = {}
    images = {}

    def path(oid: str) -> str:
        result = []
        while oid:
            result.append(names[oid])
            oid = parent.get(oid, "")
        return "/".join(reversed(result))

    def world(oid: str, point: list[float]) -> list[float]:
        while oid:
            s = blocks[transforms[oid]]
            point = [a*b for a, b in zip(point, _vector(s, "m_LocalScale"))]
            point = _rotate(_vector(s, "m_LocalRotation"), point)
            point = [a+b for a, b in zip(point, _vector(s, "m_LocalPosition"))]
            oid = parent.get(oid, "")
        return point

    def sprite(ref: str, image_name: str, purpose: str) -> dict:
        guid = re.search(r"guid: ([a-f0-9]{32})", ref)[1]
        file_id = re.search(r"fileID: (-?\d+)", ref)[1]
        asset = meta[guid]
        if asset.suffix == ".png" and file_id != "21300000":
            raise ValueError(f"Scene texture sub-sprite needs explicit importer support: {asset}#{file_id}")
        if asset not in sprite_cache:
            sprite_cache[asset] = _sprite_data(asset)
        data = sprite_cache[asset]
        images.setdefault(image_name, dict(id=image_name, purpose=purpose,
                          source_sprite=asset.relative_to(source).as_posix(),
                          source_meta=asset.with_suffix(asset.suffix + ".meta").relative_to(source).as_posix(),
                          data=data))
        return dict(sprite=asset.stem, image=image_name, guid=guid, fileId=file_id,
                    sourcePath=asset.relative_to(assets).as_posix(),
                    nativeSize=[n / data["pixels_per_unit"] for n in data["unity_rect_xywh"][2:]],
                    pixelsPerUnit=data["pixels_per_unit"], pivot=data["unity_pivot_xy"],
                    sourceRect=data["unity_rect_xywh"], textureGuid=data["texture_guid"],
                    borderUnits=data["border_units"], importerSingleSprite=asset.suffix == ".png")

    def renderer(fid: str, image_name: str, purpose: str) -> dict:
        s, oid = blocks[fid], owner[fid]
        item = sprite(_scalar(s, "m_Sprite"), image_name, purpose)
        origin = world(oid, [0, 0, 0])
        right = [a-b for a, b in zip(world(oid, [1, 0, 0]), origin)]
        up = [a-b for a, b in zip(world(oid, [0, 1, 0]), origin)]
        scale_x, scale_y = math.hypot(*right[:2]), math.hypot(*up[:2])
        if abs(right[2]) > 1e-5 or abs(up[2]) > 1e-5 or scale_x == 0 or scale_y == 0:
            raise ValueError(f"Map sprite is not a nondegenerate XY sprite: {path(oid)}")
        if abs(right[0]*up[0] + right[1]*up[1]) > 1e-5:
            raise ValueError(f"Sheared map sprite is unsupported: {path(oid)}")
        draw_mode = int(_scalar(s, "m_DrawMode"))
        size = item["nativeSize"] if draw_mode == 0 else _vector(s, "m_Size")
        flip_x, flip_y = _scalar(s, "m_FlipX") == "1", _scalar(s, "m_FlipY") == "1"
        # SpriteBatch positions describe the rectangle center, whereas Unity's
        # transform describes the pivot. Preserve this displacement under flips.
        dx = (0.5 - item["pivot"][0]) * size[0] * (-1 if flip_x else 1)
        dy = (0.5 - item["pivot"][1]) * size[1] * (-1 if flip_y else 1)
        center = [o + x*dx + y*dy for o, x, y in zip(origin, right, up)]
        item.update(gameObject=oid, renderer=fid, name=names[oid], sceneLine=lines[fid],
                    color=_vector(s, "m_Color"), drawMode=draw_mode,
                    size=_vector(s, "m_Size"), flipX=flip_x, flipY=flip_y,
                    sortingOrder=int(_scalar(s, "m_SortingOrder")),
                    worldPosition=origin, worldCenter=center, worldRight=right, worldUp=up,
                    worldSize=[size[0]*scale_x, size[1]*scale_y],
                    worldAngleRadians=math.atan2(right[1], right[0]),
                    worldFlipX=flip_x,
                    worldFlipY=flip_y ^ (right[0]*up[1]-right[1]*up[0] < 0))
        return item

    character_guid = _guid(assets / "Scripts" / "CharacterSetter.cs.meta")
    characters = []
    for fid, s in blocks.items():
        if f"guid: {character_guid}" not in s:
            continue
        oid = owner[fid]
        icon_id = next(i for i in names if parent.get(i) == oid and names[i] == "Icon")
        icon = next(c for c in components[icon_id] if "  m_Sprite:" in blocks[c])
        ref = _scalar(blocks[icon], "m_Sprite")
        name = meta[re.search(r"guid: (\w+)", ref)[1]].stem.removesuffix("_icon")
        item = sprite(ref, "player" if name == "well_rounded" else "character_" + name,
                      "Brotato.unity CharacterSetter selectable body")
        item.update(id=name, name="Well-Rounded" if name == "well_rounded" else name.title(),
                    gameObject=oid, sceneLine=lines[fid],
                    order=int(_scalar(blocks[transforms[oid]], "m_RootOrder")))
        characters.append(item)
    characters.sort(key=lambda c: c["order"])
    container_guid = _guid(assets / "Scripts" / "ContainerLogic.cs.meta")
    container = next(s for s in blocks.values() if f"guid: {container_guid}" in s)
    map_ids = re.findall(r"  - \{fileID: (\d+)\}", container.split("  Maps:\n", 1)[1])
    maps = []
    for map_index, oid in enumerate(map_ids):
        floor_id = next(fid for fid in components[oid] if types[fid] == "212")
        floor = renderer(floor_id, "map_floor", "Brotato.unity shared tinted tiled map border and floor")
        decorations = []
        for did in names:
            if not path(did).startswith(path(oid) + "/"):
                continue
            for fid in components.get(did, []):
                if types[fid] == "4":
                    continue
                if types[fid] != "212":
                    raise ValueError(f"Unexpected map decoration component: {path(did)} ({types[fid]})")
                ref = _scalar(blocks[fid], "m_Sprite")
                name = meta[re.search(r"guid: (\w+)", ref)[1]].stem
                decorations.append(renderer(fid, "map_" + name,
                                   "Brotato.unity " + names[oid] + " authored decoration"))
        decorations.sort(key=lambda d: d["sortingOrder"])
        maps.append(dict(name=names[oid], displayName=f"MAP {map_index+1}", gameObject=oid,
                         sceneLine=lines[oid], floorSprite=floor["sprite"], floor=floor,
                         decorations=decorations))
    if len(characters) != 5 or len(maps) != 5 or any(len(m["decorations"]) != 50 for m in maps):
        raise ValueError("Source scene no longer matches the audited five characters / five 50-decoration maps")
    return dict(format_version=1, source_scene="Assets/Brotato.unity",
                source_scene_sha256=hashlib.sha256(scene_path.read_bytes()).hexdigest(),
                characters=characters, maps=maps, images=list(images.values()),
                sourceMapSelection="ContainerLogic.OnEnable chooses one random map; no map-selection button is bound.",
                sourceDefaultCharacter="BodyCharacter initially has no sprite; Well-Rounded is the first selectable item.",
                sourceCharacterEffects="CharacterSetter changes only BodyCharacter.sprite, not stats.",
                geometry="XY world positions include complete hierarchy, normalized rotation and sprite-pivot compensation. Whole-texture single-sprite references are preserved.")


def _float(n: float) -> str:
    if abs(n) < 1e-9:
        n = 0.0
    value = format(n, ".9g")
    if "." not in value and "e" not in value:
        value += ".0"
    return value + "f"


def _vec(values: list[float]) -> str:
    return "{" + ", ".join(_float(n) for n in values) + "}"


def write_catalog_headers(catalog: dict, public: Path) -> None:
    public.mkdir(parents=True, exist_ok=True)
    lines = ["// Generated by tools/import_brotato_assets.py. Do not edit by hand.", "#pragma once",
             "#include <array>", "#include <cstddef>", "#include <glm/glm.hpp>", "", "namespace Brotato {",
             "struct CharacterDefinition { const char* id; const char* name; const char* image; glm::vec2 size; };",
             "inline const std::array<CharacterDefinition, 5> Characters{{"]
    for c in catalog["characters"]:
        lines.append(f'    {{"{c["id"]}", "{c["name"]}", "{c["image"]}", {_vec(c["nativeSize"])}}},')
    lines += ["}};", "struct MapDefinition { const char* id; const char* name; const char* floorImage; glm::vec4 background; glm::vec2 floorCenter; glm::vec2 floorSize; float floorAngle; bool floorFlipY; };",
              "inline const std::array<MapDefinition, 5> Maps{{"]
    for m in catalog["maps"]:
        f = m["floor"]
        lines.append(f'    {{"{m["name"]}", "{m["displayName"]}", "{f["image"]}", {_vec(f["color"])}, {_vec(f["worldCenter"][:2])}, {_vec(f["worldSize"])}, {_float(f["worldAngleRadians"])}, {str(f["worldFlipY"]).lower()}}},')
    f = catalog["maps"][0]["floor"]
    lines += ["}};", "inline constexpr std::size_t RandomMap = 5;",
              "inline const glm::vec2 MapFloorNativeSize" + _vec(f["nativeSize"]) + ";",
              "// Border order is left, bottom, right, top in the unrotated sprite.",
              "inline const glm::vec4 MapFloorBorder" + _vec(f["borderUnits"]) + ";", "} // namespace Brotato", ""]
    (public / "content_catalog.h").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    lines = ["// Generated by tools/import_brotato_assets.py. Do not edit by hand.", "#pragma once",
             '#include "content_catalog.h"', "", "namespace Brotato {",
             "struct MapDecoration { std::size_t map; const char* image; glm::vec2 position; glm::vec2 size; float angle; glm::vec4 tint; bool flipX; bool flipY; };",
             "inline const std::array<MapDecoration, 250> Decorations{{"]
    for index, m in enumerate(catalog["maps"]):
        for d in m["decorations"]:
            lines.append(f'    {{{index}, "{d["image"]}", {_vec(d["worldCenter"][:2])}, {_vec(d["worldSize"])}, {_float(d["worldAngleRadians"])}, {_vec(d["color"])}, {str(d["worldFlipX"]).lower()}, {str(d["worldFlipY"]).lower()}}},')
    lines += ["}};", "} // namespace Brotato", ""]
    (public / "map_layout_data.h").write_text("\n".join(lines), encoding="utf-8", newline="\n")
