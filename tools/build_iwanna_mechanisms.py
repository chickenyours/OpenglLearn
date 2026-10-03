"""Procedural pixel art and an additive shooting / radial-trap campaign demo.

Only the named new entities/layer and a Lua wrapper are added. Existing room
geometry and callbacks (including author edits) are retained, with one backup.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from shutil import copy2
from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parents[1]
BEGIN = "-- BEGIN PREVIOUS_ROOM_LOGIC"
END = "-- END PREVIOUS_ROOM_LOGIC"


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def build_assets() -> None:
    folder = REPO / "Asset/IWanna/images"
    for name in ("bullet", "shot_switch", "ring_plate", "ring_anchor"):
        image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        d = ImageDraw.Draw(image)
        if name == "bullet":
            d.rectangle((1, 8, 27, 23), fill="#4c5132")
            d.rectangle((3, 10, 28, 21), fill="#ffb64c")
            d.rectangle((5, 12, 30, 19), fill="#ffed9b")
            d.rectangle((7, 12, 27, 14), fill="#fff9dc")
        elif name == "shot_switch":
            d.rectangle((2, 2, 29, 29), fill="#111b29")
            d.rectangle((4, 4, 27, 27), fill="#57677b")
            d.rectangle((6, 6, 25, 25), fill="#223242")
            d.rectangle((9, 9, 22, 22), fill="#9d402b")
            d.rectangle((11, 11, 20, 20), fill="#ffd565")
            d.rectangle((12, 11, 18, 13), fill="#fff5b9")
            for x, y in ((5, 5), (24, 5), (5, 24), (24, 24)):
                d.rectangle((x, y, x + 2, y + 2), fill="#a3bdcc")
            d.line((15, 2, 15, 7), fill="#77e5dd", width=2)
        elif name == "ring_plate":
            d.rectangle((1, 23, 30, 30), fill="#202431")
            d.rectangle((3, 21, 28, 27), fill="#677180")
            d.rectangle((5, 19, 26, 24), fill="#af5260")
            d.rectangle((7, 18, 24, 21), fill="#f5878b")
            for x in (4, 12, 20, 28):
                d.line((x, 25, x - 3, 29), fill="#ffd675", width=2)
        else:
            d.ellipse((3, 3, 28, 28), outline="#8c536e", width=2)
            d.ellipse((10, 10, 21, 21), fill="#742b4c")
            d.rectangle((14, 12, 17, 19), fill="#f492a4")
            for x, y in ((15, 1), (15, 27), (1, 15), (27, 15)):
                d.rectangle((x, y, x + 3, y + 3), fill="#d67592")
        image.resize((128, 128), Image.Resampling.NEAREST).save(folder / f"demo_{name}.png")
    prefabs = REPO / "Asset/IWanna/Workshop/prefabs"
    values = {
        "shot_switch": {"role": "trigger", "image": "demo_shot_switch.png", "width": 2.5,
                        "height": 2.5, "collide": True, "receivesShots": True,
                        "event": "open_gate", "wallGroup": "tiles:ShotGate"},
        "ring_plate": {"role": "trigger", "image": "demo_ring_plate.png", "width": 2.5,
                       "height": .9, "collide": True, "event": "apple_ring",
                       "detectionEnabled": True, "detectionX": 0, "detectionY": -.55,
                       "detectionW": 2.3, "detectionH": 1.1,
                       "spawnAnchor": "ring_center", "spawnPrefab": "ring_apple", "minCount": 12, "maxCount": 24,
                       "radius": 2.5, "speed": 15, "lifetime": 2.4},
        "ring_anchor": {"role": "decoration", "image": "demo_ring_anchor.png",
                        "width": 2.2, "height": 2.2, "collide": False},
        "ring_apple": {"role": "hazard", "image": "demo_apple.png",
                       "width": 1.4, "height": 1.4, "collide": True},
    }
    for name, value in values.items():
        write_json(prefabs / f"{name}.prefab.json", {"format": "IWANNA_PREFAB_1", "pixelArt": True, **value})


def wrap_script(source: str) -> str:
    if BEGIN in source and END in source:
        source = source.split(BEGIN, 1)[1].split(END, 1)[0].strip()
    example = (REPO / "Asset/IWanna/Examples/apple_ring.lua").read_text(encoding="utf-8")
    return """-- Shooting switches; apple ring generation is implemented entirely in Lua.
local apple_ring = (function()
-- BEGIN LUA APPLE RING EXAMPLE
""" + example.rstrip() + """
-- END LUA APPLE RING EXAMPLE
end)()
local previous = (function()
""" + BEGIN + "\n" + source.rstrip() + "\n" + END + """
end)()
local result = {}
for key, callback in pairs(previous) do result[key] = callback end
result.on_hit = function(ctx, event)
  if event.name == "open_gate" and ctx:once("shot:" .. event.id) then
    ctx:set_group_enabled(event.properties.wallGroup, false)
    ctx:set_enabled(event.id, false)
    ctx:sound("BLIP")
  elseif previous.on_hit then previous.on_hit(ctx, event) end
end
result.on_trigger = function(ctx, event)
  if event.name == "apple_ring" then
    apple_ring.on_trigger(ctx, event)
  elseif previous.on_trigger then previous.on_trigger(ctx, event) end
end
result.on_timer = function(ctx, event)
  if event.name == "lua_apple_ring_expire" then
    apple_ring.on_timer(ctx, event)
  elseif previous.on_timer then previous.on_timer(ctx, event) end
end
return result
"""


def install_demo(root: Path) -> None:
    root = root.resolve()
    file = root / "rooms/room_01.room.json"
    room = json.loads(file.read_text(encoding="utf-8"))
    grid = room["grid"]
    if grid["width"] != 96 or grid["height"] != 24:
        raise RuntimeError("demo expects the authored 96x24 MyIwana first room")
    u = grid["tileSize"]
    ox, oy = grid["origin"]
    entities = room["entities"]
    backup = file.with_name(file.name + ".before_mechanisms.bak")
    if not backup.exists():
        copy2(file, backup)
    # The first island's spike is at column 6. Gate at col 9 reaches the
    # ceiling and floor, so firing from the safe pocket after that spike is required.
    gate = [[0] * grid["width"] for _ in range(grid["height"])]
    for row in range(2, 18):
        gate[row][9] = 1 + (1 if row == 2 else 0) + 2 + (4 if row == 17 else 0) + 8
    room["tileLayers"]["ShotGate"] = gate

    def add(uid: str, prefab: str, col: float, y: float, size: list[float], props: dict) -> None:
        # Preserve editor adjustments when this installer is run again.
        entities.setdefault(uid, {"kind": "Entity", "prefab": prefab,
                                  "position": [ox + col * u, y], "size": size,
                                  "properties": props})

    floor = oy + 18 * u
    add("shot_switch_01", "shot_switch", 8.2, floor - 1.25, [2.3, 2.3],
        {"wallGroup": "tiles:ShotGate", "event": "open_gate", "receivesShots": True})
    # Seventh island: plenty of reaction space and a safe pocket on the left.
    add("ring_plate_01", "ring_plate", 86.5, floor - .45, [2.5, .9],
        {"spawnAnchor": "ring_center_01", "spawnPrefab": "ring_apple", "minCount": 12, "maxCount": 24,
         "radius": 2.5, "speed": 15, "lifetime": 1.0})
    add("ring_center_01", "ring_anchor", 92.5, floor - 8, [2.2, 2.2], {})
    room.setdefault("metadata", {})["mechanisms"] = "shooting_and_radial_v1"
    script = root / room["script"]
    script_backup = script.with_name(script.name + ".before_mechanisms.bak")
    if not script_backup.exists():
        copy2(script, script_backup)
    script.write_text(wrap_script(script.read_text(encoding="utf-8")), encoding="utf-8")
    write_json(file, room)
    for name in ("shot_switch", "ring_plate", "ring_anchor", "ring_apple"):
        source = REPO / f"Asset/IWanna/Workshop/prefabs/{name}.prefab.json"
        destination = root / "prefabs" / source.name
        if not destination.exists():
            copy2(source, destination)
    for name in ("bullet", "shot_switch", "ring_plate", "ring_anchor"):
        source = REPO / f"Asset/IWanna/images/demo_{name}.png"
        destination = root / "images" / source.name
        if not destination.exists():
            copy2(source, destination)
    config_file = root / "gameplay.json"
    config = json.loads(config_file.read_text(encoding="utf-8"))
    config["player"].setdefault("shooting", {"enabled": True, "image": "demo_bullet.png",
        "width": .7, "height": .28, "speed": 80, "cooldown": .15, "lifetime": 1.8,
        "muzzleXRatio": .08, "muzzleYRatio": -.05})
    write_json(config_file, config)
    (root / "MECHANISMS.md").write_text("""# 射击与苹果环

Z / K：发射，按住连射；方向跟随人物最后的移动方向。
第一关第一个岛末端有封闭到顶的 ShotGate 墙层，射中左侧金色开关后整层消失。
触碰开关无效，开关和墙在重生时恢复，开关一直能从墙左侧射到。
末段平台入口的粉色压力板会立即在圆形标记中心生成 12～24 个苹果，径向扩散。
可以触发后向左撤回，再等 1 秒消散；或跳过压力板。不会永久封路。

编辑器 F5 刷新后可放置 shot_switch、ring_plate、ring_anchor、ring_apple。
开关 wallGroup 是 tiles:图层名，也可用实体 properties.group 指定。
压力板 spawnAnchor 是圆心实体 ID；直接拖动 ring_center_01 可以改变圆心。
spawnPrefab 是生成对象的模板 ID，默认 ring_apple；prefab 是压力板自身的模板，不能混用。
minCount/maxCount、radius、speed、lifetime 都能在属性面板调整。
player.shooting 配置在 gameplay.json 中，参数采用世界单位和秒。
Lua 逻辑在 scripts/room_01.lua，原有自定义回调保留在 previous 中。
苹果环的数量随机、圆周位置、径向速度与销毁计时均由 Lua 实现，
只调用 get_position、spawn、set_velocity、after、destroy 通用接口。
可复用源码：scripts/apple_ring_example.lua（独立使用时可作为房间脚本）。
""", encoding="utf-8")
    copy2(REPO / "Asset/IWanna/Examples/apple_ring.lua", root / "scripts/apple_ring_example.lua")
    print(f"Installed shooting gate and radial trap in {root}; original callbacks preserved")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", type=Path)
    args = parser.parse_args()
    build_assets()
    if args.project:
        install_demo(args.project)
