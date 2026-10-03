"""Add a data-authored upper route without replacing existing Lua or lower traps.

Run after the campaign/mechanism generators. The migration is deliberately
idempotent: later editor changes are never overwritten by a second invocation.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from shutil import copy2

MARKER = "two_floors_segmented_exits_v1"


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_verification(root: Path) -> None:
    def expect(x: float, floor: float) -> dict:
        return {"room": "room_01", "alive": True, "grounded": True,
                "position": [x, floor - 1.1], "tolerance": [.3, .15]}
    steps = []
    for move, x, floor in [(1, 110, 10), (-1, 100, 0), (1, 110, -10), (-1, 100, -20)]:
        steps.extend([{"move": move, "jump": True, "jumpHeld": True, "frames": 27},
                      {"move": move, "frames": 40},
                      {"frames": 30, "expect": expect(x, floor)}])
    steps.extend([{"move": 1, "jump": True, "jumpHeld": True, "frames": 30},
                  {"move": 1, "jump": True, "jumpHeld": True, "frames": 27},
                  {"move": 1, "frames": 63},
                  {"frames": 40, "expect": expect(118, -25)}])
    cases = [{"name": "climb all stairs continuously", "room": "room_01",
              "position": [100, 18.9], "settleFrames": 30, "steps": steps}]
    for name, y, room in [("walk through upper exit", -26.1, "room_02"),
                           ("walk through lower exit", 18.9, "room_03")]:
        cases.append({"name": name, "room": "room_01", "position": [226, y],
                      "settleFrames": 15, "steps": [{"move": 1, "frames": 32}],
                      "expect": {"room": room, "alive": True}})
    probes = []
    for y in (-64, -46, -45, -26.1, -25, -15, -.01, 0, 18.9, 20, 34):
        target = "room_02" if -45 <= y < -25 else "room_03" if 0 <= y < 20 else None
        probe = {"name": f"right Y={y}", "room": "room_01", "position": [230.1, y]}
        if target:
            probe["expectRoom"] = target
        probes.append(probe)
    for name, point in [("left", [-10.1, 10]), ("top", [100, -65.1]), ("bottom", [100, 35.1])]:
        probes.append({"name": name, "room": "room_01", "position": point})
    write_json(root / "TWO_FLOOR_INPUT_CHECK.json", {"format": "IWANNA_INPUT_REPLAY_1",
               "noPortals": ["room_01"], "cases": cases, "boundaries": probes})
    # Individual upper-route jumps supplement the continuous stair replay.
    jumps = []
    for index, (x, floor, tx, tfloor, second) in enumerate([
        (100, 20, 110, 10, None), (110, 10, 100, 0, None),
        (100, 0, 110, -10, None), (110, -10, 100, -20, None),
        (100, -20, 118, -25, .25), (127, -25, 145, -30, .25),
        (145, -30, 158, -30, None), (165, -25, 180, -25, None),
        (177, -25, 186, -32.5, None), (185, -32.5, 198, -32.5, None),
        (207, -25, 220, -25, None)]):
        jumps.append({"section": index, "kind": "upper_route", "fromColumn": (x + 10) / 2.5,
                      "toColumn": (tx + 10) / 2.5, "fromRow": (floor + 65) / 2.5,
                      "toRow": (tfloor + 65) / 2.5, "secondJumpAt": second})
    write_json(root / "TWO_FLOOR_JUMP_CHECK.json", {"format": "IWANNA_ROUTE_EVIDENCE_1",
               "rooms": {"room_01": jumps}, "timedTraps": {}})


def install(root: Path) -> None:
    path = root / "rooms/room_01.room.json"
    room = json.loads(path.read_text(encoding="utf-8"))
    if room.get("metadata", {}).get("floorLayout") == MARKER:
        print("Two-floor layout already installed; preserving editor changes")
        return
    grid = room["grid"]
    if grid != {"width": 96, "height": 24, "tileSize": 2.5, "origin": [-10, -25]}:
        raise ValueError("Expected the 96x24 campaign room at [-10,-25]; refusing to move a custom grid")
    backup = path.with_name(path.name + ".before_two_floors.bak")
    if not backup.exists():
        copy2(path, backup)

    # Add forty world units above the existing room. Existing entities, lower
    # tiles, gate and Lua retain their exact world coordinates and identifiers.
    grid["origin"][1] -= 40
    grid["height"] += 16
    for name, rows in room["tileLayers"].items():
        room["tileLayers"][name] = [[0] * 96 for _ in range(16)] + rows
    terrain = room["tileLayers"]["Terrain"]

    def rect(left: int, right: int, top: int, bottom: int, value: int = 1) -> None:
        for y in range(top, bottom):
            for x in range(left, right):
                terrain[y][x] = value

    rect(0, 96, 0, 2)  # Upper ceiling.
    rect(42, 50, 16, 34, 0)  # Open a shaft beside the existing mid checkpoint.
    stairs = [(47, 49, 30), (43, 45, 26), (47, 49, 22), (43, 45, 18)]
    for left, right, row in stairs:
        rect(left, right, row, row + 1)
    rect(60, 68, 14, 16)  # Raised upper platforms, 5 and 7.5 world units.
    rect(77, 84, 13, 16)

    # Upper floor uses the existing blue stone edge variants. Both floors still
    # use the same ordinary solid tile representation and collision system.
    for edge in range(16):
        room["palette"][str(33 + edge)] = f"terrain_azure_{edge:02d}.png"
        room["palette"][str(49 + edge)] = f"terrain_azure_alt_{edge:02d}.png"
    occupied = [[bool(cell) for cell in row] for row in terrain]
    for y, row in enumerate(terrain):
        for x, cell in enumerate(row):
            if not cell:
                continue
            def filled(cx: int, cy: int) -> bool:
                return 0 <= cx < 96 and 0 <= cy < 40 and occupied[cy][cx]
            edges = (0 if filled(x, y - 1) else 1) | (0 if filled(x + 1, y) else 2)
            edges |= (0 if filled(x, y + 1) else 4) | (0 if filled(x - 1, y) else 8)
            alternate = ((x * 17 + y * 37 + 29) % 11) < 3
            upper = y < 18 or (42 <= x < 50 and y < 34)
            row[x] = (33 if upper else 1) + edges + (16 if alternate else 0)

    entities = room["entities"]
    removed = []
    for uid, entity in list(entities.items()):
        prefab = entity.get("prefab")
        template_file = root / "prefabs" / (str(prefab) + ".prefab.json")
        template = json.loads(template_file.read_text(encoding="utf-8")) if template_file.is_file() else {}
        role = entity.get("properties", {}).get("role", template.get("role"))
        if role == "exit":
            removed.append(uid)
            del entities[uid]
    entities["checkpoint_upper"] = {
        "kind": "Entity", "prefab": "checkpoint", "position": [120, -27.5],
        "size": [4, 4.8], "properties": {"detectionEnabled": True,
        "detectionX": 0, "detectionY": 0, "detectionW": 4, "detectionH": 4.8}}
    entities["upper_start"] = {"kind": "Spawn", "position": [120, -26.1], "properties": {}}
    for index, (x, floor) in enumerate([(133.75, -25), (151.25, -30),
                                       (172.5, -25), (190, -32.5), (213.75, -25)], 1):
        entities[f"upper_spike_{index:02d}"] = {"kind": "Entity", "prefab": "spike",
            "position": [x, floor - 1.25], "size": [2.5, 2.5], "properties": {}}
    # Transfer only through the openings next to each floor. Neither adjacency
    # nor the order of rooms in world.json carries any gameplay meaning.
    room["connections"] = {}
    room["boundaries"] = {edge: {"action": "death"} for edge in ("left", "top", "bottom")}
    room["boundaries"]["right"] = {"action": "death", "segments": [
        {"range": [-45, -25], "action": "transfer", "room": "room_02", "spawn": "start"},
        {"range": [0, 20], "action": "transfer", "room": "room_03", "spawn": "start"}]}
    room["title"] = "The Forked Causeway"
    metadata = room.setdefault("metadata", {})
    metadata["floorLayout"] = MARKER
    metadata["removedPortals"] = removed
    metadata["floorRoutes"] = {
        "upper": {"destination": "room_02/start", "floorY": -25, "exitY": [-45, -25]},
        "lower": {"destination": "room_03/start", "floorY": 20, "exitY": [0, 20]},
        "stairs": {"shaftX": [95, 115], "landings": [[110, 10], [100, 0], [110, -10], [100, -20]]}}
    for island in metadata.get("routeIslands", []):
        island[2] += 16
    write_json(path, room)
    write_verification(root)
    schema = Path(__file__).resolve().parents[1] / "Asset/IWanna/Workshop/schemas/room.schema.json"
    if (root / "schemas").is_dir():
        copy2(schema, root / "schemas/room.schema.json")

    evidence_file = root / "CAMPAIGN_ROUTE_CHECK.json"
    if evidence_file.is_file():
        evidence = json.loads(evidence_file.read_text(encoding="utf-8"))
        evidence_backup = evidence_file.with_suffix(".json.before_two_floors.bak")
        if not evidence_backup.exists():
            copy2(evidence_file, evidence_backup)
        for jump in evidence.get("rooms", {}).get("room_01", []):
            jump["fromRow"] += 16
            jump["toRow"] += 16
        # The existing Lua is authoritative (the author changed apple speed).
        # Keep the evidence in sync without replacing any of that script.
        lua = (root / "scripts/room_01.lua").read_text(encoding="utf-8")
        import re
        velocity = re.search(r"ctx:set_velocity\(trap\.id,\s*0,\s*([0-9.]+)\)", lua)
        if velocity:
            for trap in evidence.get("timedTraps", {}).get("room_01", []):
                if "velocityY" in trap:
                    trap["velocityY"] = float(velocity[1])
                    trap.pop("straightRunHitAt", None)  # No longer a verified timestamp.
        write_json(evidence_file, evidence)
    (root / "TWO_FLOOR_LAYOUT.md").write_text("""# 第一房间：上下两楼

第一房间传送门已移除。原下层地形、射击开关和 Lua 苹果机关保留。
中段存档点旁 x=95～115 的竖井有四块交替踏板，逐级跳跃可到上层；也可沿原下层继续前进。
上层新增存档点、蓝色石砖平台和尖刺，右端通往第二房间 start。
下层右端通往第三房间 start。第二、第三房间自身的出口保持原设定。

房间格子：96×40；世界左上角 [-10,-65]，右下角 [230,35]。
right 的两个有效出口段（世界 Y，包含下限、不含上限）：
- [-45,-25) → room_02/start
- [0,20) → room_03/start

left/top/bottom 及 right 上的其他高度全部死亡。
规则只存于 rooms/room_01.room.json 的 boundaries，不在 Lua/C++ 中写死房间关系。
编辑器 Ctrl+L 可修改 right[-45,-25]=room_02/start 等区段，Tab 查看拓扑。
预览上层可选择 upper_start 出生点；normal start 仍在原下层起点。

重新生成完整关卡时，最后运行 tools/upgrade_myiwanna_two_floors.py 项目目录。
迁移保留 before_two_floors.bak，重复运行不会覆盖编辑器后续修改。
""", encoding="utf-8")
    design_file = root / "LEVEL_DESIGN.md"
    if design_file.is_file():
        previous_design = design_file.read_text(encoding="utf-8")
        design_file.write_text("# 当前拓扑：第一房间双层分流\n\n"
            "第一房间现为 96×40，上层右边界 → room_02，下层右边界 → room_03，其他边界死亡。\n"
            "中段踏板连接两楼，第一房间已无传送门。配置及验证方法见 [TWO_FLOOR_LAYOUT.md](TWO_FLOOR_LAYOUT.md)。\n"
            "下文是原始下层跑酷设计记录：第一房间世界坐标未变，格子行号在当前地图中增加 16；第二、第三房间未改动。\n\n"
            + previous_design, encoding="utf-8")
    print(f"Installed two floors and split boundary exits in {root}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("project", type=Path)
    install(parser.parse_args().project.resolve())
