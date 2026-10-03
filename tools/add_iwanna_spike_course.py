"""Extend room_02 upward with a connected Lua spike challenge; retain authored rooms."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from shutil import copy2

MARKER = "lua_spike_course_v1"


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def write_verification(root: Path) -> None:
    bait = [{"move": 1, "frames": 165}, {"move": -1, "frames": 24},
            {"frames": 120}, {"move": 1, "frames": 260}]
    double = [{"move": 1, "jump": True, "jumpHeld": True, "frames": 48},
              {"move": 1, "jump": True, "jumpHeld": True, "frames": 70}, {"frames": 65}]
    shaft = [{"move": 1, "frames": 47}, {"frames": 200},
             {"move": 1, "frames": 85}, {"frames": 200},
             {"move": -1, "frames": 80}, {"frames": 140}]
    def expected(x: float, y: float, tolerance: float = .5) -> dict:
        return {"alive": True, "grounded": True, "position": [x, y], "tolerance": [tolerance, .3]}
    def case(name: str, spawn: str, steps: list, expect: dict, position: list | None = None) -> dict:
        result = {"name": name, "room": "room_02", "spawn": spawn,
                  "settleFrames": 10, "steps": steps, "expect": expect}
        if position is not None:
            result["position"] = position
        return result
    cases = [
        case("bait the upward spike then cross", "spike_course", bait, expected(56.4, -61.1)),
        case("upward spike punishes standing in its path", "spike_course",
             [{"frames": 120, "expect": {"alive": False}}], {"alive": False}, [25, -61.1]),
        case("double jump clears the left spike and high step", "spike_left", double,
             expected(102.7, -76.1, 1), [85, -61.1]),
        case("single jump cannot clear the left spike", "spike_left",
             [{"move": 1, "jump": True, "jumpHeld": True, "frames": 118, "expect": {"alive": False}}],
             {"alive": False}, [85, -61.1]),
        case("side-step through the flying spike shaft", "spike_corridor", shaft, expected(-3.75, 18.9)),
        case("whole spike course without repositioning", "spike_course",
             bait + [{"move": 1, "frames": 191}] + double + [{"move": 1, "frames": 99}] + shaft,
             expected(-3.75, 18.9)),
    ]
    write(root / "SPIKE_TRAPS_INPUT_CHECK.json", {"format": "IWANNA_INPUT_REPLAY_1", "cases": cases})


def install(root: Path) -> None:
    path = root / "rooms/room_02.room.json"
    room = json.loads(path.read_text(encoding="utf-8-sig"))
    if room.get("metadata", {}).get("spikeCourse") == MARKER:
        print("Spike course already installed; retaining editor changes")
        return
    grid = room["grid"]
    if grid != {"width": 96, "height": 24, "tileSize": 2.5, "origin": [-10, -25]}:
        raise ValueError("Expected the original room_02 grid; refusing to move custom geometry")
    if any(key.startswith("spike_course_") for key in room["entities"]):
        raise ValueError("Spike course IDs are already in use")
    backup = path.with_name(path.name + ".before_spike_course.bak")
    if not backup.exists():
        copy2(path, backup)
    # Padding above the room changes grid indices, never existing world positions.
    for name, rows in room["tileLayers"].items():
        room["tileLayers"][name] = [[0] * 96 for _ in range(32)] + rows
    grid["origin"][1] = -105
    grid["height"] = 56
    tiles = [[0] * 96 for _ in range(56)]
    room["tileLayers"]["SpikeCourse"] = tiles

    def rect(x0: float, x1: float, y0: float, y1: float) -> None:
        for row in range(round((y0 + 105) / 2.5), round((y1 + 105) / 2.5)):
            for col in range(round((x0 + 10) / 2.5), round((x1 + 10) / 2.5)):
                tiles[row][col] = 1

    rect(-10, 102.5, -60, -55)       # Upper gallery floor.
    rect(-10, 50, -100, -67.5)       # Low, unjumpable ceiling over bait trap.
    rect(-10, -7.5, -100, -60)
    rect(50, 142.5, -100, -97.5)
    rect(102.5, 122.5, -75, -55)    # 15-unit step: single jump cannot reach it.
    rect(120, 122.5, -72.5, -30)    # Narrow descending passage.
    rect(140, 142.5, -97.5, -30)
    rect(122.5, 132.5, -60, -57.5)  # Alternate left/right waiting ledges.
    rect(130, 140, -45, -42.5)
    rect(120, 142.5, -32.5, -30)
    occupied = [[bool(v) for v in row] for row in tiles]
    for row, values in enumerate(tiles):
        for col, value in enumerate(values):
            if not value:
                continue
            def filled(x: int, y: int) -> bool:
                return 0 <= x < 96 and 0 <= y < 56 and occupied[y][x]
            mask = (0 if filled(col, row-1) else 1) | (0 if filled(col+1, row) else 2)
            mask |= (0 if filled(col, row+1) else 4) | (0 if filled(col-1, row) else 8)
            values[col] = 33 + mask + (16 if (col*17+row*37) % 11 < 3 else 0)
    for mask in range(16):
        room["palette"][str(33+mask)] = f"terrain_azure_{mask:02d}.png"
        room["palette"][str(49+mask)] = f"terrain_azure_alt_{mask:02d}.png"

    entities = room["entities"]
    def entity(uid: str, prefab: str, x: float, y: float, w: float, h: float, **props) -> None:
        entities[uid] = {"kind": "Entity", "prefab": prefab, "position": [x, y],
                         "size": [w, h], "properties": props}
    def spawn(uid: str, x: float, y: float) -> None:
        entities[uid] = {"kind": "Spawn", "position": [x, y], "properties": {}}
    def checkpoint(uid: str, x: float, floor: float) -> None:
        entity(uid, "checkpoint", x, floor-2.5, 4, 4.8)

    spawn("spike_course", -3.75, -61.1)
    spawn("spike_left", 70, -61.1)
    spawn("spike_corridor", 117.5, -76.1)
    checkpoint("spike_course_checkpoint_start", -3.75, -60)
    checkpoint("spike_course_checkpoint_left", 70, -60)
    checkpoint("spike_course_checkpoint_corridor", 117.5, -75)
    # Optional branch beside the ordinary start; no change to the four-room route.
    entity("spike_course_entrance", "door", -7.5, 17.5, 2, 5,
           destinationRoom="$self", destinationSpawn="spike_course")
    entity("spike_course_return", "door", 125.5, -35, 3, 5,
           destinationRoom="$self", destinationSpawn="start")
    checkpoint("spike_course_return_checkpoint", 0, 20)

    # The source sprite has transparent padding. Inset the root so even the
    # largest stretch and its sub-frame center drift stay inside solid terrain.
    entity("spike_course_up", "floor_spike_trap", 25, -59.5, 1.8, .5,
           effectAnchor="spike_course_up", orientation="up", thickness=1,
           length=9, restLength=.4, delay=.25, extendSeconds=.18,
           holdSeconds=.15, retractSeconds=.35, cooldown=2,
           detectionEnabled=True, detectionX=0, detectionY=-4.25,
           detectionW=7, detectionH=7.5)
    entity("spike_course_left", "left_spike_trap", 103.8, -68, .5, 2,
           effectAnchor="spike_course_left", orientation="left", thickness=2.5,
           length=24, restLength=.5, delay=.35, extendSeconds=.22,
           holdSeconds=.8, retractSeconds=.5, cooldown=1.5,
           detectionEnabled=True, detectionX=-20.3, detectionY=5,
           detectionW=5, detectionH=8)
    entity("spike_course_flying_upper", "flying_spike_trap", 139.5, -66.5, .6, 1.5,
           effectAnchor="spike_course_flying_upper", orientation="left",
           laneCount=3, laneSpacing=3, count=2, speed=24, warningSeconds=.5,
           length=2.5, thickness=1.2, flightDistance=12, lifetime=1.5, cooldown=1.5, seed=0,
           detectionEnabled=True, detectionX=-8.25, detectionY=-2,
           detectionW=17.5, detectionH=5)
    entity("spike_course_flying_lower", "flying_spike_trap", 123, -51.5, .6, 1.5,
           effectAnchor="spike_course_flying_lower", orientation="right",
           laneCount=3, laneSpacing=3, count=2, speed=24, warningSeconds=.5,
           length=2.5, thickness=1.2, flightDistance=12, lifetime=1.5, cooldown=1.5, seed=0,
           detectionEnabled=True, detectionX=8.25, detectionY=-2,
           detectionW=17.5, detectionH=5)

    metadata = room.setdefault("metadata", {})
    metadata["spikeCourse"] = MARKER
    metadata["spikeCourseDescription"] = "Bait the low-ceiling up spike; double-jump the 15-unit rise; dodge through the alternating flying-spike shaft."
    for island in metadata.get("routeIslands", []):
        island[2] += 32
    write(path, room)
    evidence_path = root / "CAMPAIGN_ROUTE_CHECK.json"
    if evidence_path.exists():
        evidence = json.loads(evidence_path.read_text(encoding="utf-8-sig"))
        evidence_backup = evidence_path.with_name(evidence_path.name + ".before_spike_course.bak")
        if not evidence_backup.exists():
            copy2(evidence_path, evidence_backup)
        for jump in evidence.get("rooms", {}).get("room_02", []):
            jump["fromRow"] += 32
            jump["toRow"] += 32
        write(evidence_path, evidence)
    for suffix, point in [("", "spike_course"), ("_left", "spike_left"), ("_corridor", "spike_corridor")]:
        (root / f"run_spike_demo{suffix}.bat").write_text(
            f'@echo off\ncall "%~dp0run.bat" --room room_02 --spawn {point} %*\n', encoding="ascii")
    write_verification(root)
    print(f"Connected spike course added above room_02: {root}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    install(parser.parse_args().project.resolve())
