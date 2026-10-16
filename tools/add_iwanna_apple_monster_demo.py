"""Create an isolated apple monster arena, retaining all existing room files."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from install_iwanna_apple_monster import install_project, local_path


def install(root: Path) -> None:
    root = root.resolve()
    world = json.loads((root/"world.json").read_text(encoding="utf-8-sig"))
    if world.get("format") != "IWANNA_WORLD_2":
        raise ValueError("Expected an IWANNA_WORLD_2 project")
    room_path = local_path(root,world["roomDirectory"])/"apple_monster_demo.room.json"
    script_path = local_path(root,"scripts/apple_monster_demo.lua")
    if room_path.exists() or script_path.exists():
        raise ValueError("Apple monster arena already exists; retaining developer edits")
    tiles = [[0]*40 for _ in range(28)]
    for y,row in enumerate(tiles):
        for x in range(40):
            if y >= 22 or y == 0 or x < 2 or x >= 38:
                row[x] = 1
    for row,start in [(17,6),(17,30)]:
        for x in range(start,start+4):
            tiles[row][x] = 1
    def entity(prefab, x, y, w, h, **p):
        return {"kind":"Entity","prefab":prefab,"position":[x,y],"size":[w,h],"properties":p}
    room = {
        "format":"IWANNA_ROOM_2","title":"Apple Monster","hint":"",
        "grid":{"width":40,"height":28,"tileSize":2.5,"origin":[-50,-35]},
        "camera":{"mode":"fixed","position":[0,-6],"zoom":1.5,"clampToRoom":False},
        "palette":{"1":"terrain_azure_00.png"},"tileLayers":{"Terrain":tiles},
        "boundaries":{},"script":"scripts/apple_monster_demo.lua",
        "entities":{
            "start":{"kind":"Spawn","position":[-20,18.9],"properties":{}},
            "checkpoint_start":entity("checkpoint",-20,17.5,4,4.8),
            "apple_summoner":entity("apple_monster_trap",0,19.55,2.5,.9,
                arenaMinX=-40,arenaMaxX=40,arenaMinY=-24,arenaMaxY=16.5),
            "return_to_campaign":entity("door",41,17,3,6,
                destinationRoom=world["startRoom"],destinationSpawn=world["startSpawn"]),
        },
        "metadata":{"authoring":"lua_apple_monster_demo_v1"},
    }
    script_path.parent.mkdir(parents=True,exist_ok=True)
    script_path.write_text("-- Apple monster behavior is embedded by the installer.\nreturn {}\n",encoding="utf-8")
    room_path.write_text(json.dumps(room,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    install_project(root,("apple_monster_demo",))
    (root/"run_apple_monster_demo.bat").write_text(
        '@echo off\ncall "%~dp0run.bat" --room apple_monster_demo --spawn start %*\n',encoding="ascii")
    print(f"Added apple_monster_demo in {root}; original rooms retained")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project",required=True,type=Path)
    install(parser.parse_args().project)
