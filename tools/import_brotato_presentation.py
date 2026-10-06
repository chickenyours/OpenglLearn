#!/usr/bin/env python3
"""Import the bound Brotato Transform animations and presentation sprites.

The Unity input is read-only. Controllers, scene/prefab component references,
curve tangents, sprite pivots, and source hashes remain in a separate manifest.
No AnimationClip is selected merely because its filename looks relevant.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re

from PIL import Image
from brotato_scene import _sprite_data, _rotate, read_scene

NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"


def scalar(text: str, key: str, indent: int = 2) -> str:
    match = re.search(r"^" + " " * indent + re.escape(key) + r": (.*)$", text, re.M)
    if not match:
        raise ValueError(f"Missing Unity field {key}")
    return match[1]


def vector(value: str) -> list[float]:
    return [float(n) for n in re.findall(r": (" + NUMBER + ")", value)]


def guid(path: Path) -> str:
    return re.search(r"^guid: ([a-f0-9]{32})$", path.read_text(encoding="utf-8-sig"), re.M)[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_id(ref: str) -> str:
    return re.search(r"fileID: (-?\d+)", ref)[1]


def ref_guid(ref: str) -> str:
    return re.search(r"guid: ([a-f0-9]{32})", ref)[1]


def blocks(path: Path) -> dict[str, tuple[str, str]]:
    text = path.read_text(encoding="utf-8-sig")
    matches = list(re.finditer(r"^--- !u!(\d+) &(-?\d+).*\n", text, re.M))
    return {match[2]: (match[1], text[match.end():matches[i + 1].start()
            if i + 1 < len(matches) else len(text)]) for i, match in enumerate(matches)}


def parse_clip(path: Path) -> dict:
    text = path.read_text(encoding="utf-8-sig")
    if scalar(text, "m_PPtrCurves") != "[]":
        raise ValueError(f"Sprite keyframes require a separate importer: {path}")
    result = dict(name=path.stem, duration=float(scalar(text, "m_StopTime", 4)),
                  loop=scalar(text, "m_LoopTime", 4) == "1", tracks=[])
    fields = {"Position": [("x", "PositionX"), ("y", "PositionY")],
              "Scale": [("x", "ScaleX"), ("y", "ScaleY")],
              "Euler": [("z", "AngleDegrees")]}
    floats = {"m_Color.a": "Opacity", "m_AnchoredPosition.x": "PositionX",
              "m_AnchoredPosition.y": "PositionY"}
    for kind in ("Euler", "Position", "Scale", "Float"):
        section = re.search(r"^  m_" + kind + r"Curves:.*?(?=^  m_)", text, re.M | re.S)[0]
        for curve in re.split(r"(?=^  - curve:)", section, flags=re.M)[1:]:
            node = scalar(curve, "path", 4)
            raw_keys = []
            for entry in re.split(r"(?=^      - serializedVersion:)", curve, flags=re.M)[1:]:
                if scalar(entry, "weightedMode", 8) != "0":
                    raise ValueError(f"Weighted tangents are unsupported: {path}")
                data = {key: scalar(entry, key, 8) for key in ("time", "value", "inSlope", "outSlope")}
                raw_keys.append(data)
            channels = [(None, floats[scalar(curve, "attribute", 4)])] if kind == "Float" else fields[kind]
            for axis, channel in channels:
                def component(value: str) -> float:
                    return float(value) if axis is None else float(re.search(r"\b" + axis + r": (" + NUMBER + ")", value)[1])
                keys = [[float(key["time"]), component(key["value"]),
                         component(key["inSlope"]), component(key["outSlope"])] for key in raw_keys]
                if not keys or any(a[0] >= b[0] for a, b in zip(keys, keys[1:])):
                    raise ValueError(f"Invalid key times: {path}:{node}")
                result["tracks"].append(dict(path=node, channel=channel, keys=keys))
    return result


def import_presentation(source: Path, output: Path, catalog_output: Path) -> dict:
    source = source.resolve(strict=True)
    output, catalog_output = output.resolve(), catalog_output.resolve()
    for destination in (output, catalog_output):
        if destination == source or source in destination.parents:
            raise ValueError("Output must be outside the read-only Unity source project")
    assets = source / "Assets"
    paths = {guid(meta): meta.with_suffix("") for directory in
             ("AnimatorController", "AnimationClip", "Sprite", "Texture2D", "PrefabInstance")
             for meta in sorted((assets / directory).glob("*.meta"))}
    scene_path, enemy_path = assets / "Brotato.unity", assets / "AI/NpcGo.prefab"
    scene, enemy = blocks(scene_path), blocks(enemy_path)
    selectable_characters = read_scene(source)["characters"]
    records, prepared, inputs = [], [], {scene_path, enemy_path}
    # Verify the scene/prefab scripts actually point at the Animator instances
    # being imported. A controller existing in Assets is not sufficient proof.
    for document, script, animator_id in ((scene, "PlayerController", "5083329934025401204"),
                                          (enemy, "EnemyNPC", "4613876897586908232")):
        script_path = assets / "Scripts" / (script + ".cs")
        script_guid = guid(script_path.with_suffix(".cs.meta"))
        behaviours = [text for kind, text in document.values() if kind == "114" and
                      f"guid: {script_guid}" in text]
        if len(behaviours) != 1 or file_id(scalar(behaviours[0], "_anim")) != animator_id:
            raise ValueError(f"Actual {script} Animator binding has changed")
        inputs.add(script_path)
    visual_refs = {
        "8a67fdfe278a1934ebe69c722a0b1cff": ("player_leg_right", "PlayerLegRight"),
        "6698a93cc47dbf1448f7b2a558df9324": ("player_leg_left", "PlayerLegLeft"),
        "1237c20eb29691147b40991fcf397659": ("player_shadow", "PlayerShadow"),
        "8fb67f3c0310a944185653f57e4edb8c": ("player_mark", "PlayerMark"),
        "32c6618a43a1a34409acccbf6662fe18": ("hit_particle", "HitParticle"),
    }
    for sprite_guid, (name, enum) in visual_refs.items():
        sprite = paths[sprite_guid]
        data = _sprite_data(sprite)
        texture = paths[data["texture_guid"]]
        with Image.open(texture) as original:
            atlas = original.convert("RGBA")
        x, y, width, height = data["unity_rect_xywh"]
        box = (math.floor(x), atlas.height - math.ceil(y + height), math.ceil(x + width), atlas.height - math.floor(y))
        if not (0 <= box[0] < box[2] <= atlas.width and 0 <= box[1] < box[3] <= atlas.height):
            raise ValueError(f"Invalid sprite rectangle: {sprite}")
        image = atlas.crop(box)
        if not image.getchannel("A").getbbox():
            raise ValueError(f"Transparent source sprite: {sprite}")
        prepared.append((name, image))
        records.append(dict(id=name, image_enum=enum, output=name + ".png", source_sprite=sprite.relative_to(source).as_posix(),
                            source_sprite_guid=sprite_guid, source_sprite_sha256=digest(sprite),
                            source_texture=texture.relative_to(source).as_posix(), source_texture_sha256=digest(texture),
                            **data, crop_top_left_ltrb=list(box), output_size_px=list(image.size),
                            unity_size_units=[width / data["pixels_per_unit"], height / data["pixels_per_unit"]]))

    def nodes(document, animator_id, enemy_rig=False):
        animator = document[animator_id][1]
        root = file_id(scalar(animator, "m_GameObject"))
        components, transforms, children = {}, {}, {}
        for fid, (kind, text) in document.items():
            if kind == "1":
                components[fid] = re.findall(r"^  - component: \{fileID: (\d+)\}", text, re.M)
            elif kind == "4":
                owner = file_id(scalar(text, "m_GameObject"))
                transforms[owner] = fid
                children.setdefault(file_id(scalar(text, "m_Father")), []).append(owner)
        result = []
        def visit(owner, parent, path):
            transform = document[transforms[owner]][1]
            position = vector(scalar(transform, "m_LocalPosition"))
            scale = vector(scalar(transform, "m_LocalScale"))
            node = dict(path=path, game_object_file_id=owner, parent=parent, position=position[:2], scale=scale[:2],
                        angle=0.0, opacity=1.0, image="Enemy" if enemy_rig else "Player", visible=False,
                        size=[1.0, 1.0], pivot=[.5, .5], tint=[1.0]*4, order=0)
            for component in components[owner]:
                kind, text = document[component]
                if kind != "212" or file_id(scalar(text, "m_Sprite")) == "0":
                    continue
                sprite_guid = ref_guid(scalar(text, "m_Sprite"))
                sprite_path = paths[sprite_guid]
                data = _sprite_data(sprite_path)
                node.update(visible=scalar(text, "m_Enabled") == "1", source_sprite=sprite_path.relative_to(source).as_posix(),
                            source_sprite_guid=sprite_guid, size=[n / data["pixels_per_unit"] for n in data["unity_rect_xywh"][2:]],
                            pivot=data["unity_pivot_xy"], tint=vector(scalar(text, "m_Color")), order=int(scalar(text, "m_SortingOrder")))
                if path == "Player_Body_All/Body":
                    node["image"] = "PlayerBody"  # Fixed potato1 mask; selection is the Itemes child.
                elif sprite_guid in visual_refs:
                    node["image"] = visual_refs[sprite_guid][1]
                elif not enemy_rig:
                    raise ValueError(f"Unmapped actor sprite: {path}")
            if path == "Player_Body_All/Body/Itemes":
                # All five CharacterSetter.BodyCharacter fields point to this
                # initially empty renderer, not the fixed Body mask renderer.
                renderer = next(fid for fid in components[owner] if document[fid][0] == "212")
                setters = [text for kind, text in scene.values() if kind == "114" and
                           re.search(r"^  BodyCharacter: \{fileID: " + renderer + r"\}$", text, re.M)]
                if len(setters) != 5:
                    raise ValueError("Expected five CharacterSetter bindings to the Itemes overlay")
                node.update(image="Player", visible=True, size=selectable_characters[0]["nativeSize"],
                            pivot=selectable_characters[0]["pivot"],
                            note="Initially null renderer selected by five CharacterSetter.BodyCharacter references; migration chooses Well-Rounded as default.")
            index = len(result)
            result.append(node)
            for child in children.get(transforms[owner], []):
                name = scalar(document[child][1], "m_Name")
                # ParticleSystem smoke needs a separate simulation; it has no
                # SpriteRenderer or actor clip bindings and is not a rig node.
                if name != "Smoke":
                    visit(child, index, path + "/" + name if path else name)
        visit(root, -1, "")
        return result

    player_nodes = nodes(scene, "5083329934025401204")
    enemy_nodes = nodes(enemy, "4613876897586908232", True)
    clips, bindings = {}, []
    def bound_clip(document, animator_id, state, name, rig):
        controller_path = paths[ref_guid(scalar(document[animator_id][1], "m_Controller"))]
        controller = blocks(controller_path)
        candidates = [text for kind, text in controller.values() if kind == "1102" and scalar(text, "m_Name") == state]
        if len(candidates) != 1:
            raise ValueError(f"Expected one bound {state} state in {controller_path}")
        if float(scalar(candidates[0], "m_Speed")) != 1:
            raise ValueError("Animator speed requires explicit import support")
        clip_path = paths[ref_guid(scalar(candidates[0], "m_Motion"))]
        clip = parse_clip(clip_path)
        node_indices = {node["path"]: i for i, node in enumerate(rig)}
        for track in clip["tracks"]:
            if track["path"] not in node_indices:
                raise ValueError(f"Unresolved actual animator path {track['path']}")
            track["node"] = node_indices[track["path"]]
        clips[name] = clip
        inputs.update((controller_path, clip_path))
        bindings.append(dict(id=name, animator_file_id=animator_id, controller=controller_path.relative_to(source).as_posix(),
                             controller_guid=guid(controller_path.with_suffix(".controller.meta")), state=state,
                             clip=clip_path.relative_to(source).as_posix(), clip_guid=guid(clip_path.with_suffix(".anim.meta"))))

    bound_clip(scene, "5083329934025401204", "Idle", "PlayerIdle", player_nodes)
    bound_clip(scene, "5083329934025401204", "Move", "PlayerMove", player_nodes)
    bound_clip(enemy, "4613876897586908232", "Move", "EnemyMove", enemy_nodes)
    bound_clip(enemy, "4613876897586908232", "Death", "EnemyDeath", enemy_nodes)
    # Resolve the actual Value Animator by its GameObject, not a guessed file ID.
    if "DamageText" not in clips:
        value_animators = [fid for fid, (kind, text) in enemy.items() if kind == "95" and
                           scalar(enemy[file_id(scalar(text, "m_GameObject"))][1], "m_Name") == "Value"]
        if len(value_animators) != 1:
            raise ValueError("Expected one Value Animator")
        bound_clip(enemy, value_animators[0], "AddIt", "DamageText", [dict(path="")])
    # Find an actual scene Shoot Animator before importing its flash curve.
    flash_candidates = []
    for fid, (kind, text) in scene.items():
        if kind == "95" and "m_Controller:" in text:
            controller_path = paths.get(ref_guid(scalar(text, "m_Controller")))
            if controller_path and controller_path.name == "Shoot.controller":
                flash_candidates.append(fid)
    if not flash_candidates:
        raise ValueError("No actual Shoot Animator binding")
    flash_controller = blocks(paths[ref_guid(scalar(scene[flash_candidates[0]][1], "m_Controller"))])
    flash_states = [(scalar(t, "m_Name"), paths[ref_guid(scalar(t, "m_Motion"))])
                    for kind, t in flash_controller.values() if kind == "1102"]
    flash_state = next(state for state, path in flash_states if path.name == "BulletAnim.anim")
    bound_clip(scene, flash_candidates[0], flash_state, "MuzzleFlash", [dict(path="")])

    particle_path = paths[ref_guid(scalar(enemy["4703139939605592405"][1], "PrefabParticle"))]
    particle_text = particle_path.read_text(encoding="utf-8-sig")
    if "guid: 32c6618a43a1a34409acccbf6662fe18" not in particle_text:
        raise ValueError("Hit particle UV sprite binding changed")
    inputs.add(particle_path)
    # Value lives under Canvas directly on the NPC root, outside Monster_All's
    # death animation. Canvas depth is a world-Y offset after root's -90 X turn.
    canvas = enemy["7041146916155741641"][1]
    canvas_position = vector(scalar(canvas, "m_LocalPosition"))
    canvas_position[:2] = vector(scalar(canvas, "m_AnchoredPosition"))
    root_transform = enemy[file_id(scalar(canvas, "m_Father"))][1]
    canvas_world_offset = _rotate(vector(scalar(root_transform, "m_LocalRotation")), canvas_position)
    manifest = dict(format_version=1, source_project="Unity2021_Botato", operation="Original RGBA crops and unweighted Hermite Transform curves",
                    bindings=bindings, player_nodes=player_nodes, enemy_nodes=enemy_nodes, clips=clips, assets=records,
                    characters=[dict(id=c["id"], image=c["image"], pivot=c["pivot"]) for c in selectable_characters],
                    hit_particles=dict(source_prefab=particle_path.relative_to(source).as_posix(), count=6, lifetime=.5,
                                       speed_range=[1, 8], size_range=[.1, .28], tint=[.8018868, .19290671, .1970778, 1],
                                       size_normalized_keys=[[0, 1, 0, 0], [1, 0, -2, -2]], cone_angle_degrees=7, radius=.1,
                                       note="Source uses a fixed +X 3D cone; the 2D ECS projection is documented separately."),
                    damage_text=dict(base_position=[-.039, -.758], parent_offset=canvas_world_offset[:2],
                                     parent_rect_transform="7041146916155741641", parent_local_position=canvas_position,
                                     tint=[1]*4, font_size=.25, displayed_range=[1, 8],
                                     note="Cosmetic random +1..8, not damage or experience. Source keeps endpoint until corpse removal."),
                    excluded=["Player Death is not played by PlayerController; gameplay freezes on death.",
                              "Player Smoke ParticleSystem requires a separate emitter/material migration."],
                    source_files=[dict(path=path.relative_to(source).as_posix(), sha256=digest(path)) for path in sorted(inputs)])
    header = generate_header(manifest)
    output.mkdir(parents=True, exist_ok=True)
    catalog_output.mkdir(parents=True, exist_ok=True)
    for (name, image), record in zip(prepared, records):
        image.save(output / (name + ".png"), format="PNG", optimize=False)
        record["output_sha256"] = digest(output / (name + ".png"))
    (output / "presentation_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
    (catalog_output / "animation_catalog.h").write_text(header, encoding="utf-8", newline="\n")
    return manifest


def generate_header(data: dict) -> str:
    def f(value):
        text = format(float(value), ".9g")
        if "." not in text and "e" not in text:
            text += ".0"
        return text + "f"
    def v(values):
        return "{" + ", ".join(map(f, values)) + "}"
    lines = ["// Generated by tools/import_brotato_presentation.py. Do not edit by hand.", "#pragma once",
             '#include "Render/Public/Sprite/sprite_animation.h"', '#include "Brotato/Public/weapon_definitions.h"',
             "#include <array>", "", "namespace Brotato {", "struct AnimatedNode {",
             "    int parent; Render::Animation::Pose base; Image image; bool visible;",
             "    glm::vec2 size, pivot; glm::vec4 tint; int order;", "};"]
    for name, nodes in (("PlayerNodes", data["player_nodes"]), ("EnemyNodes", data["enemy_nodes"])):
        lines.append(f"inline const std::array<AnimatedNode, {len(nodes)}> {name}{{{{")
        for node in nodes:
            lines.append("    // " + (node["path"] or "Animator root"))
            pose = "{" + v(node["position"]) + ", " + v(node["scale"]) + ", " + f(node["angle"]) + ", " + f(node["opacity"]) + "}"
            lines.append(f"    {{{node['parent']}, {pose}, Image::{node['image']}, {str(node['visible']).lower()}, " +
                         v(node["size"]) + ", " + v(node["pivot"]) + ", " + v(node["tint"]) + f", {node['order']}}},")
        lines.append("}};")
    lines.append(f"inline const std::array<glm::vec2, {len(data['characters'])}> CharacterPivots{{{{")
    lines.extend("    " + v(character["pivot"]) + ", // " + character["id"] for character in data["characters"])
    lines.append("}};")
    for name, clip in data["clips"].items():
        for i, track in enumerate(clip["tracks"]):
            lines.append(f"inline constexpr std::array<Render::Animation::Key, {len(track['keys'])}> {name}Keys{i}{{{{")
            lines.extend("    " + v(key) + "," for key in track["keys"])
            lines.append("}};")
        lines.append(f"inline const std::array<Render::Animation::Track, {len(clip['tracks'])}> {name}Tracks{{{{")
        for i, track in enumerate(clip["tracks"]):
            lines.append(f"    {{{track['node']}, Render::Animation::Channel::{track['channel']}, {name}Keys{i}}},")
        lines.append("}};")
        lines.append(f"inline const Render::Animation::Clip {name}{{{f(clip['duration'])}, {str(clip['loop']).lower()}, {name}Tracks}};")
    lines += ["inline const Render::Animation::Pose DamageTextBase{{-.039f, -.758f}, {1.f, 1.f}, 0.f, 1.f};",
              "inline const glm::vec2 DamageTextParentOffset" + v(data["damage_text"]["parent_offset"]) + ";",
              "inline constexpr float DamageTextFontSize = .25f;", "} // namespace Brotato", ""]
    return "\n".join(lines)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "Asset/Brotato")
    parser.add_argument("--catalog-output", type=Path, default=Path(__file__).resolve().parents[1] / "Module/Brotato/Public")
    args = parser.parse_args()
    imported = import_presentation(args.source, args.output, args.catalog_output)
    print(f"Imported {len(imported['assets'])} presentation sprites and {len(imported['clips'])} bound clips")
