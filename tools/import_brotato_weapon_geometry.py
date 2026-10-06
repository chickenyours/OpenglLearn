#!/usr/bin/env python3
"""Import bound weapon roots, sprites and firing geometry; Unity stays read-only."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from brotato_scene import _rotate, _sprite_data
from import_brotato_assets import texture_sprite_data
from import_brotato_presentation import digest, file_id, guid, parse_clip, ref_guid, scalar, vector
from import_brotato_selection import Scene


def import_geometry(source: Path, output: Path, catalog_output: Path) -> dict:
    source = source.resolve(strict=True)
    output, catalog_output = output.resolve(), catalog_output.resolve()
    for destination in (output, catalog_output):
        if destination == source or source in destination.parents:
            raise ValueError("Output must be outside the read-only Unity source project")
    assets = source / "Assets"
    paths = {guid(meta): meta.with_suffix("") for directory in ("Sprite", "Texture2D", "PrefabInstance")
             for meta in sorted((assets / directory).glob("*.meta"))}
    inputs = set()

    def document(path):
        inputs.add(path)
        if path.with_suffix(path.suffix + ".meta").exists():
            inputs.add(path.with_suffix(path.suffix + ".meta"))
        doc = Scene(path)
        doc.by_owner = {}
        for fid, (kind, text) in doc.blocks.items():
            doc.by_owner.setdefault(doc.owners.get(fid), []).append((fid, kind, text))
        return doc

    scene = document(assets / "Brotato.unity")

    def position(doc, owner, point, stop=None):
        point = list(point)
        while owner != stop:
            if owner is None:
                if stop is not None:
                    raise ValueError("Referenced geometry is not below the bound root")
                break
            text = doc.blocks[doc.transforms[owner]][1]
            point = [a*b for a, b in zip(point, vector(scalar(text, "m_LocalScale")))]
            point = _rotate(vector(scalar(text, "m_LocalRotation")), point)
            point = [a+b for a, b in zip(point, vector(scalar(text, "m_LocalPosition")))]
            owner = doc.parents.get(owner)
        return point

    def transform(doc, owner, stop=None):
        origin = position(doc, owner, [0, 0, 0], stop)
        axes = [[b-a for a,b in zip(origin, position(doc, owner, unit, stop))]
                for unit in ([1,0,0], [0,1,0])]
        # A flat sprite must remain a non-singular rectangle after XY projection.
        det = axes[0][0]*axes[1][1]-axes[0][1]*axes[1][0]
        if abs(det) < 1e-6:
            raise ValueError("Unsupported edge-on sprite plane: " + doc.path(owner))
        return dict(offset=origin[:2], axis_x=axes[0][:2], axis_y=axes[1][:2])

    def evidence(doc, owner):
        fid = doc.transforms[owner]
        text = doc.blocks[fid][1]
        return dict(path=doc.path(owner), game_object_file_id=owner, transform_file_id=fid,
                    scene_line=doc.lines[fid], local_position=vector(scalar(text,"m_LocalPosition")),
                    local_rotation_xyzw=vector(scalar(text,"m_LocalRotation")),
                    local_scale=vector(scalar(text,"m_LocalScale")))

    def sprite(doc, renderer, stop):
        fid, _, text = renderer
        if scalar(text,"m_DrawMode") != "0":
            raise ValueError("Weapon renderer must be a simple Sprite")
        reference = scalar(text,"m_Sprite")
        asset = paths[ref_guid(reference)]
        if asset.suffix == ".asset":
            data = _sprite_data(asset)
        else:
            data = texture_sprite_data(asset.with_suffix(asset.suffix + ".meta"), int(file_id(reference)))
        inputs.update((asset, asset.with_suffix(asset.suffix + ".meta")))
        texture = paths[data["texture_guid"]]
        inputs.update((texture, texture.with_suffix(texture.suffix + ".meta")))
        owner = doc.owners[fid]
        geometry = transform(doc, owner, stop)
        # SpriteRenderer flips operate about the Sprite pivot.
        if scalar(text,"m_FlipX") == "1": geometry["axis_x"] = [-v for v in geometry["axis_x"]]
        if scalar(text,"m_FlipY") == "1": geometry["axis_y"] = [-v for v in geometry["axis_y"]]
        geometry.update(size=[v/data["pixels_per_unit"] for v in data["unity_rect_xywh"][2:]],
                        pivot=data["unity_pivot_xy"])
        return dict(geometry=geometry, renderer_file_id=fid, transform=evidence(doc,owner),
                    source_sprite=asset.relative_to(source).as_posix(), sprite_reference=reference,
                    sprite_metadata=data)

    def visible_sprites(doc, root):
        prefix = doc.path(root) + "/"
        return [entry for owner, entries in doc.by_owner.items()
                if owner == root or (owner and doc.path(owner).startswith(prefix))
                for entry in entries if entry[1] == "212" and file_id(scalar(entry[2],"m_Sprite")) != "0"]

    player = scene.paths["Container/PlayerGo"]
    player_origin = scene.world(player,[0,0,0])
    weapons, beams, burst = [], [], []
    for index in range(1,7):
        kind = ("Wand","Torch","Laser","Knife","Gun","Burst")[index-1]
        owner = scene.paths[f"Container/PlayerGo/ws/w{index}"]
        bindings = [entry for entry in scene.by_owner[owner] if entry[1] == "114" and "  shootingCooldown:" in entry[2]]
        if len(bindings) != 1: raise ValueError("Expected one bound weapon behaviour")
        script_id, _, script = bindings[0]
        script_path = assets / "Scripts" / f"w{index}.cs"
        inputs.update((script_path,script_path.with_suffix(".cs.meta")))
        if ref_guid(scalar(script,"m_Script")) != guid(script_path.with_suffix(".cs.meta")):
            raise ValueError("Unexpected weapon script binding")
        rotator_id = file_id(scalar(script,"weapon" if index in (2,4) else "WeaponRotator"))
        root = scene.owners.get(rotator_id,rotator_id)
        # All ancestors/root have unit scale in the recovered source. Runtime
        # AngleAxis assigns WORLD rotation, so descendants exclude this root's
        # authored quaternion instead of projecting its initial local rotation.
        ancestor = root
        while ancestor:
            scale = vector(scalar(scene.blocks[scene.transforms[ancestor]][1],"m_LocalScale"))
            if any(abs(v-1)>1e-6 for v in scale): raise ValueError("Weapon root requires new scale support")
            ancestor = scene.parents.get(ancestor)
        root_origin = scene.world(root,[0,0,0])
        initial = transform(scene,root)
        x, y = initial["axis_x"], initial["axis_y"]
        if any(abs(value) > 1e-6 for value in (sum(v*v for v in x)-1, sum(v*v for v in y)-1, sum(a*b for a,b in zip(x,y)))):
            raise ValueError("Initial weapon rotation requires a full affine root basis")
        idle_angle = math.atan2(initial["axis_x"][1],initial["axis_x"][0])
        idle_reflected = initial["axis_x"][0]*initial["axis_y"][1]-initial["axis_x"][1]*initial["axis_y"][0] < 0
        body_renderers = [entry for entry in visible_sprites(scene,root)
                          if paths[ref_guid(scalar(entry[2],"m_Sprite"))].suffix == ".asset"]
        if len(body_renderers) != 1: raise ValueError("Expected one bound weapon body")
        record = dict(kind=kind, script_file_id=script_id, script_owner=evidence(scene,owner),
                      rotation_root=evidence(scene,root),
                      script_offset=[a-b for a,b in zip(scene.world(owner,[0,0,0])[:2],player_origin[:2])],
                      root_offset=[a-b for a,b in zip(root_origin[:2],player_origin[:2])],
                      idle_angle=idle_angle, idle_reflected=idle_reflected,
                      body=sprite(scene,body_renderers[0],root), fire_point=[0.,0.],
                      projectile=None, muzzle=None, muzzle_parent=[0.,0.], muzzle_parent_x=[1.,0.], muzzle_parent_y=[0.,1.])
        if index in (1,5,6):
            fire_id = file_id(scalar(script,"TransformShoot"))
            fire_owner = scene.owners.get(fire_id,fire_id)
            record["fire_point"] = position(scene,fire_owner,[0,0,0],root)[:2]
            record["fire_transform"] = evidence(scene,fire_owner)
            if index in (5,6):
                muzzle_id = file_id(scalar(script,"ShootPrefabe"))
                muzzle_owner = scene.owners.get(muzzle_id,muzzle_id)
                renderers = [entry for entry in scene.by_owner[muzzle_owner] if entry[1] == "212"]
                parent = scene.parents[muzzle_owner]
                record["muzzle"] = sprite(scene,renderers[0],parent)
                parent_pose = transform(scene,parent,root)
                record.update(muzzle_parent=parent_pose["offset"], muzzle_parent_x=parent_pose["axis_x"], muzzle_parent_y=parent_pose["axis_y"])
            prefab_ref = scalar(script,"bulletPrefab")
            prefab = document(paths[ref_guid(prefab_ref)])
            prefab_root = file_id(prefab_ref)
            renderers = visible_sprites(prefab,prefab_root)
            if index != 6:
                if len(renderers)!=1: raise ValueError("Expected one projectile Sprite")
                record["projectile"] = sprite(prefab,renderers[0],prefab_root)
            else:
                clip_path = assets / "AnimationClip/MoveGun.anim"
                inputs.update((clip_path, clip_path.with_suffix(".anim.meta")))
                clip = parse_clip(clip_path)
                if len(renderers)!=4: raise ValueError("Expected four authored pellets")
                ordered = sorted(renderers,key=lambda e:int(scalar(prefab.blocks[prefab.transforms[prefab.owners[e[0]]]][1],"m_RootOrder")))
                for renderer in ordered:
                    child = prefab.owners[renderer[0]]
                    tracks = {track["channel"]:track for track in clip["tracks"] if track["path"]==prefab.names[child]}
                    xy = [tracks[channel]["keys"] for channel in ("PositionX","PositionY")]
                    if any(len(keys)!=2 or abs(keys[0][0])>1e-6 or abs(keys[-1][0]-.25)>1e-6 or
                           any(abs(k[2])+abs(k[3])>1e-6 for k in keys) for keys in xy):
                        raise ValueError("Pellet motion requires a new animation sampler")
                    pose = transform(prefab,child,prefab_root)
                    burst.append(dict(start=[keys[0][1] for keys in xy], end=[keys[-1][1] for keys in xy],
                                      angle=math.atan2(pose["axis_x"][1],pose["axis_x"][0]),
                                      sprite=sprite(prefab,renderer,child)))
                record["projectile"] = burst[0]["sprite"]
                if any(p["sprite"]["geometry"] != burst[0]["sprite"]["geometry"] for p in burst):
                    raise ValueError("Distinct pellet sprites need per-path geometry")
        if index == 3:
            list_section = script.split("  listLasers:\n")[1].split("  shootingCooldown:")[0]
            import re
            for ref in re.findall(r"fileID: (\d+)",list_section):
                beam_owner = scene.owners.get(ref,ref)
                renderer = [entry for entry in scene.by_owner[beam_owner] if entry[1]=="212"][0]
                collider = [entry for entry in scene.by_owner[beam_owner] if entry[1]=="61"][0]
                center = position(scene,beam_owner,[0,0,0],root)[:2]
                geometry = sprite(scene,renderer,root)
                beams.append(dict(center=center, sprite=geometry, collider_file_id=collider[0],
                                  collider_offset=vector(scalar(collider[2],"m_Offset")),
                                  collider_size=vector(scalar(collider[2],"m_Size"))))
        weapons.append(record)
    if len(beams)!=6 or len(burst)!=4: raise ValueError("Incomplete weapon geometry")
    manifest = dict(format_version=1, source_scene="Assets/Brotato.unity",
                    coordinate_rule="World XY. Runtime rotation overrides the bound rotator world quaternion; descendants retain authored transforms. Initial pose retains reflected XY basis.",
                    weapons=weapons, beam_segments=beams, burst_paths=burst,
                    inputs=[dict(path=p.relative_to(source).as_posix(),sha256=digest(p)) for p in sorted(inputs)])

    def f(value):
        value=0.0 if abs(value)<1e-8 else value
        s=format(value,'.9g')
        return s+('f' if '.' in s or 'e' in s else '.f')
    def v(value): return '{'+', '.join(f(x) for x in value)+'}'
    def g(record):
        if record is None: return '{}'
        data=record['geometry']
        return '{'+', '.join(v(data[key]) for key in ('offset','axis_x','axis_y','size','pivot'))+'}'
    header = ['#pragma once','// Generated by tools/import_brotato_weapon_geometry.py; do not edit.','#include <array>','#include <glm/glm.hpp>','namespace Brotato {',
        'struct LocalSpriteGeometry { glm::vec2 offset{0}, axisX{1,0}, axisY{0,1}, size{1}, pivot{.5f}; };',
        'struct WeaponGeometry { glm::vec2 scriptOffset, rootOffset; float idleAngle; bool idleReflected; LocalSpriteGeometry body; glm::vec2 firePoint; LocalSpriteGeometry projectile, muzzle; glm::vec2 muzzleParent, muzzleParentX, muzzleParentY; };',
        'inline const std::array<WeaponGeometry, 6> SourceWeaponGeometry{{']
    for w in weapons:
        header.append('    {'+', '.join((v(w['script_offset']),v(w['root_offset']),f(w['idle_angle']),str(w['idle_reflected']).lower(),g(w['body']),v(w['fire_point']),g(w['projectile']),g(w['muzzle']),v(w['muzzle_parent']),v(w['muzzle_parent_x']),v(w['muzzle_parent_y'])))+'}, // '+w['kind'])
    header += ['}};','struct BeamSegmentGeometry { glm::vec2 center; LocalSpriteGeometry sprite; };','inline const std::array<BeamSegmentGeometry, 6> SourceBeamSegments{{']
    header += ['    {'+v(b['center'])+', '+g(b['sprite'])+'},' for b in beams]
    header += ['}};','inline const std::array<float, 6> LaserCenters{'+', '.join(f(b['center'][0]) for b in beams)+'};',
               'struct BurstPath { glm::vec2 start, end; float angle; };','inline const std::array<BurstPath, 4> BurstPaths{{']
    header += ['    {'+', '.join((v(p['start']),v(p['end']),f(p['angle'])))+'},' for p in burst]
    header += ['}};','} // namespace Brotato','']
    output.mkdir(parents=True,exist_ok=True); catalog_output.mkdir(parents=True,exist_ok=True)
    (output/'weapon_geometry_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    (catalog_output/'weapon_geometry_catalog.h').write_text('\n'.join(header),encoding='utf-8',newline='\n')
    return manifest


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path)
    parser.add_argument('--output',type=Path,default=Path('Asset/Brotato'))
    parser.add_argument('--catalog-output',type=Path,default=Path('Module/Brotato/Public'))
    args=parser.parse_args()
    result=import_geometry(args.source,args.output,args.catalog_output)
    print(f"Imported {len(result['weapons'])} weapon roots, {len(result['beam_segments'])} beam segments and {len(result['burst_paths'])} pellet paths")
