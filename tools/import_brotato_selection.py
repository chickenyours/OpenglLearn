#!/usr/bin/env python3
"""Import the actual Brotato weapon/difficulty selection bindings and UI icons.

The source has six mutually exclusive starter weapons, not six equipment slots.
This importer follows onClick targets, verifies that contract, and preserves the
source's incomplete difficulty UI as evidence rather than inventing modifiers.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re

from PIL import Image
from brotato_scene import _sprite_data, _rotate, _vec
from import_brotato_presentation import blocks, digest, file_id, guid, ref_guid, scalar, vector


class Scene:
    def __init__(self, path: Path):
        self.blocks = blocks(path)
        self.names = {fid: scalar(text, "m_Name") for fid, (kind, text) in self.blocks.items() if kind == "1"}
        self.owners = {fid: file_id(scalar(text, "m_GameObject")) for fid, (_, text) in self.blocks.items()
                       if "  m_GameObject:" in text}
        self.transforms = {self.owners[fid]: fid for fid, (kind, _) in self.blocks.items() if kind in ("4", "224")}
        transform_owners = {fid: owner for owner, fid in self.transforms.items()}
        self.parents = {owner: transform_owners.get(file_id(scalar(self.blocks[fid][1], "m_Father")))
                        for owner, fid in self.transforms.items()}
        self.paths = {self.path(owner): owner for owner in self.names}
        text = path.read_text(encoding="utf-8-sig")
        self.lines = {}
        previous, line = 0, 1
        for match in re.finditer(r"^--- !u!\d+ &(-?\d+).*\n", text, re.M):
            line += text.count("\n", previous, match.start())
            self.lines[match[1]] = line
            previous = match.start()

    def path(self, fid: str) -> str:
        owner = self.owners.get(fid, fid)
        if owner not in self.names:
            return "<null>" if owner == "0" else "<unresolved:" + owner + ">"
        result = []
        while owner:
            result.append(self.names[owner])
            owner = self.parents.get(owner)
        return "/".join(reversed(result))

    def active(self, owner: str) -> bool:
        return scalar(self.blocks[owner][1], "m_IsActive") == "1"

    def components(self, owner: str):
        return [(fid, kind, text) for fid, (kind, text) in self.blocks.items() if self.owners.get(fid) == owner]

    def world(self, owner: str, point: list[float]) -> list[float]:
        while owner:
            transform = self.blocks[self.transforms[owner]][1]
            point = [a * b for a, b in zip(point, vector(scalar(transform, "m_LocalScale")))]
            point = _rotate(vector(scalar(transform, "m_LocalRotation")), point)
            point = [a + b for a, b in zip(point, vector(scalar(transform, "m_LocalPosition")))]
            owner = self.parents.get(owner)
        return point

    def buttons(self) -> list[dict]:
        result = []
        for fid, (kind, text) in self.blocks.items():
            if kind != "114" or "  m_OnClick:" not in text:
                continue
            calls = []
            for call in text.split("      - m_Target: ")[1:]:
                target = file_id(call.splitlines()[0])
                calls.append(dict(target_file_id=target, target_path=self.path(target),
                                  target_type=scalar(call, "m_TargetAssemblyTypeName", 8),
                                  method=scalar(call, "m_MethodName", 8),
                                  mode=int(scalar(call, "m_Mode", 8)),
                                  boolean=scalar(call, "m_BoolArgument", 10) == "1",
                                  call_state=int(scalar(call, "m_CallState", 8))))
            owner = self.owners[fid]
            result.append(dict(component_file_id=fid, game_object_file_id=owner, path=self.path(owner),
                               scene_line=self.lines[fid], active_self=self.active(owner),
                               interactable=scalar(text, "m_Interactable") == "1", calls=calls))
        return result


def import_selection(source: Path, output: Path, catalog_output: Path) -> dict:
    source = source.resolve(strict=True)
    output, catalog_output = output.resolve(), catalog_output.resolve()
    for destination in (output, catalog_output):
        if destination == source or source in destination.parents:
            raise ValueError("Output must be outside the read-only Unity source project")
    assets = source / "Assets"
    scene_path = assets / "Brotato.unity"
    scene = Scene(scene_path)
    buttons = scene.buttons()
    prefix = "Canvas/StartGame/"
    weapon_content = prefix + "DownUIWeapons/ScroolView/Scroll View/Viewport/Content/"
    difficulty_content = prefix + "DownUIDifficulty/ScroolView/Scroll View/Viewport/Content/"
    weapons = [scene.paths[f"Container/PlayerGo/ws/w{index}"] for index in range(1, 7)]
    weapon_buttons = [button for button in buttons if button["path"].startswith(weapon_content)]
    weapon_buttons.sort(key=lambda button: int(scalar(scene.blocks[scene.transforms[button["game_object_file_id"]]][1], "m_RootOrder")))
    if len(weapon_buttons) != 6:
        raise ValueError("Expected the six actual starter-weapon buttons")
    relevant = [button for button in buttons if button["path"].startswith(prefix) and
                any(part in button["path"] for part in ("DownUIWeapons", "DownUIDifficulty", "BackBtnOne", "DownUICharacter/NextBtn"))]
    by_path = {button["path"]: button for button in buttons}
    difficulty_buttons = [button for button in buttons if button["path"].startswith(difficulty_content)]
    if len(difficulty_buttons) != 1:
        raise ValueError("Source no longer has exactly one usable difficulty button")
    all_weapon_calls = [(button, call) for button in buttons for call in button["calls"]
                        if call["target_file_id"] in weapons]
    if len(all_weapon_calls) != 36 or any(button not in weapon_buttons for button, _ in all_weapon_calls):
        raise ValueError("Additional weapon activation rules require a new audit")

    paths = {guid(meta): meta.with_suffix("") for directory in ("Sprite", "Texture2D")
             for meta in sorted((assets / directory).glob("*.meta"))}
    prepared = []
    image_records = []
    inputs = {scene_path, *sorted((assets / "Scripts").glob("*.cs")), *sorted((assets / "Scripts").glob("*.cs.meta"))}

    def icon(button: dict, image_name: str) -> dict:
        owner = scene.paths[button["path"] + "/Icon"]
        matches = [(fid, text) for fid, kind, text in scene.components(owner) if kind == "114" and "  m_Sprite:" in text]
        if len(matches) != 1:
            raise ValueError("Expected one UI Image for " + button["path"])
        component, text = matches[0]
        reference = scalar(text, "m_Sprite")
        sprite_guid = ref_guid(reference)
        sprite = paths[sprite_guid]
        data = _sprite_data(sprite)
        texture = paths[data["texture_guid"]]
        with Image.open(texture) as original:
            atlas = original.convert("RGBA")
        x, y, width, height = data["unity_rect_xywh"]
        crop = (math.floor(x), atlas.height - math.ceil(y + height), math.ceil(x + width), atlas.height - math.floor(y))
        if not (0 <= crop[0] < crop[2] <= atlas.width and 0 <= crop[1] < crop[3] <= atlas.height):
            raise ValueError("Invalid source sprite bounds: " + str(sprite))
        image = atlas.crop(crop)
        if not image.getchannel("A").getbbox():
            raise ValueError("Transparent selection icon: " + str(sprite))
        record = dict(id=image_name, output=image_name + ".png", image_component_file_id=component,
                      source_sprite=sprite.relative_to(source).as_posix(), source_sprite_guid=sprite_guid,
                      source_sprite_file_id=file_id(reference), source_texture=texture.relative_to(source).as_posix(),
                      source_sprite_sha256=digest(sprite), source_texture_sha256=digest(texture),
                      **data, crop_top_left_ltrb=list(crop), output_size_px=list(image.size))
        image_records.append(record)
        prepared.append((image_name, image))
        inputs.update((sprite, sprite.with_suffix(sprite.suffix + ".meta"), texture, texture.with_suffix(texture.suffix + ".meta")))
        return record

    kinds = ("Wand", "Torch", "Laser", "Knife", "Gun", "Burst")
    selection = []
    player_position = scene.world(scene.paths["Container/PlayerGo"], [0, 0, 0])
    for index, (button, weapon, kind) in enumerate(zip(weapon_buttons, weapons, kinds)):
        activations = [call for call in button["calls"] if call["target_file_id"] in weapons]
        if (len(activations) != 6 or {call["target_file_id"] for call in activations} != set(weapons) or
            any(call["method"] != "SetActive" or call["mode"] != 6 or call["call_state"] != 2 or
                call["boolean"] != (call["target_file_id"] == weapon) for call in activations)):
            raise ValueError("Weapon selection is no longer mutually exclusive: " + button["path"])
        image_name = "select_" + kind.lower()
        image = icon(button, image_name)
        position = scene.world(weapon, [0, 0, 0])
        selection.append(dict(kind=kind, image=image_name, button_component_file_id=button["component_file_id"],
                              button_path=button["path"], weapon_game_object_file_id=weapon,
                              weapon_path=scene.path(weapon), weapon_active_self=scene.active(weapon),
                              player_relative_origin=[a - b for a, b in zip(position, player_position)],
                              source_icon=image["source_sprite"], active_weapon_indices=[index],
                              size=[value / image["pixels_per_unit"] for value in image["unity_rect_xywh"][2:]]))
    difficulty_button = difficulty_buttons[0]
    difficulty_icon = icon(difficulty_button, "difficulty_0")
    if not difficulty_icon["source_sprite"].endswith("/diff_0.asset"):
        raise ValueError("The only bound difficulty icon has changed")
    if any(call["method"] != "SetActive" for call in difficulty_button["calls"]):
        raise ValueError("New difficulty behavior requires a gameplay audit")
    difficulty_select = scene.paths[difficulty_button["path"] + "/Select"]
    manifest = dict(format_version=1, source_scene="Assets/Brotato.unity", source_scene_sha256=digest(scene_path),
                    weapons=selection, difficulties=[dict(id="diff_0", name="DIFFICULTY 0", image="difficulty_0",
                    size=[value / difficulty_icon["pixels_per_unit"] for value in difficulty_icon["unity_rect_xywh"][2:]],
                    button_component_file_id=difficulty_button["component_file_id"], selected_active_self=scene.active(difficulty_select),
                    source_name=None, description="The source binds diff_0.asset but has no difficulty name or modifiers.")],
                    buttons=relevant, assets=image_records,
                    rules=dict(selection="Exactly one of w1..w6 is enabled; selecting another disables the other five.",
                               weapon_default="All six source weapon roots are inactive until a weapon button is clicked.",
                               six_slot_status="Weapons(1/6) is display-only; there is no six-slot equipment, duplication, sale or replacement logic.",
                               weapon_next_initially_active=by_path[prefix + "DownUIWeapons/NextBtn"]["active_self"],
                               character_next_initially_active=by_path[prefix + "DownUICharacter/NextBtn"]["active_self"],
                               difficulty_next_initially_active=by_path[prefix + "DownUIDifficulty/NextBtn"]["active_self"],
                               difficulty="Only one item is a Button. Its calls toggle UI GameObjects, including four null targets; there are no gameplay modifiers.",
                               difficulty_default="The only item's Select and Next are already active when the difficulty panel opens.",
                               navigation="Character Next -> weapon; weapon Next -> difficulty; difficulty Next -> StartGame, enable Container and StartSpawningS.",
                               weapon_back="Back buttons change page visibility but do not clear the active weapon. Weapon Next hides highlights and itself.",
                               migration="DIFFICULTY 0 is a readable label inferred from diff_0.asset, not an original text label; no multipliers are added."),
                    source_files=[dict(path=path.relative_to(source).as_posix(), sha256=digest(path))
                                  for path in sorted(inputs, key=lambda path: path.as_posix())])
    # Validate all inputs before writing generated outputs.
    output.mkdir(parents=True, exist_ok=True)
    catalog_output.mkdir(parents=True, exist_ok=True)
    for image_name, image in prepared:
        image.save(output / (image_name + ".png"))
    for image in image_records:
        image["output_sha256"] = digest(output / image["output"])
    (output / "selection_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    lines = ["// Generated by tools/import_brotato_selection.py. Do not edit by hand.", "#pragma once",
             '#include "Brotato/Public/weapon_definitions.h"', "#include <array>", "", "namespace Brotato {",
             "// The source enables exactly one starter weapon; these are choices, not slots.",
             "struct WeaponSelectionDefinition { WeaponKind kind; const char* image; glm::vec2 size; };",
             "inline constexpr std::array<WeaponSelectionDefinition, WeaponCount> WeaponSelections{{"]
    lines += [f'    {{WeaponKind::{item["kind"]}, "{item["image"]}", {_vec(item["size"])}}},' for item in selection]
    lines += ["}};", "// Only diff_0 has a button binding; no source difficulty modifiers exist.",
              "struct DifficultyDefinition { const char* id; const char* name; const char* image; glm::vec2 size; };",
              "inline constexpr std::array<DifficultyDefinition, 1> Difficulties{{",
              '    {"diff_0", "DIFFICULTY 0", "difficulty_0", ' + _vec(manifest["difficulties"][0]["size"]) + '},',
              "}};", "} // namespace Brotato", ""]
    (catalog_output / "selection_catalog.h").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "Asset/Brotato")
    parser.add_argument("--catalog-output", type=Path, default=Path(__file__).resolve().parents[1] / "Module/Brotato/Public")
    args = parser.parse_args()
    imported = import_selection(args.source, args.output, args.catalog_output)
    print(f"Imported {len(imported['assets'])} selection icons; verified six exclusive weapons and one bound difficulty")
