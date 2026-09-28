"""Migrate a legacy world into standalone, filename-identified room/prefab assets.
Usage: py tools/migrate_iwanna_world.py Asset/IWanna/Showcase/world.json Asset/IWanna/Workshop
Never overwrites an existing destination.
"""
import json,sys
from pathlib import Path

def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def props(obj):return {p['name']:p['value'] for p in obj.get('properties',[])}
def migrate(source,target):
    if target.exists():raise ValueError('Destination already exists; migration never overwrites it')
    world=json.loads(source.read_text(encoding='utf-8-sig'));root=source.parent
    for name,prefab in world['prefabs'].items():write(target/'prefabs'/f'{name}.prefab.json',dict(format='IWANNA_PREFAB_1',**prefab))
    for item in world['rooms']:
        path=root/item['map'];m=json.loads(path.read_text(encoding='utf-8-sig'));p=props(m);unit=p['worldTileSize'];scale=unit/m['tilewidth'];origin=[p['originX'],p['originY']]
        r=dict(format='IWANNA_ROOM_2',title=item['title'],hint=item['hint'],grid=dict(width=m['width'],height=m['height'],tileSize=unit,origin=origin),palette={},tileLayers={},entities={},connections={},scriptLua=(root/item['script']).read_text(encoding='utf-8-sig'))
        for ref in m['tilesets']:
            tileset=json.loads((path.parent/ref['source']).read_text(encoding='utf-8-sig')) if 'source' in ref else ref
            for tile in tileset['tiles']:r['palette'][str(ref['firstgid']+tile['id'])]=props(tile)['runtimeImage']
        for layer in m['layers']:
            if layer['type']=='tilelayer':r['tileLayers'][layer.get('name','Terrain')]=[layer['data'][i:i+m['width']] for i in range(0,len(layer['data']),m['width'])]
            else:
                for o in layer['objects']:
                    p=props(o);uid=p.pop('uid',f"label_{o['id']}");kind=o.get('class',o.get('type','Entity'));size=[o.get('width',0)*scale,o.get('height',0)*scale]
                    e=dict(kind=kind,position=[origin[0]+o['x']*scale+size[0]/2,origin[1]+o['y']*scale+size[1]/2],properties=p)
                    if kind=='Entity':e['prefab']=p.pop('prefab');e['size']=size
                    if p.get('destinationRoom')==item['id']:p['destinationRoom']='$self'
                    r['entities'][uid]=e
        write(target/'rooms'/f"{item['id']}.room.json",r)
    write(target/'world.json',dict(format='IWANNA_WORLD_2',startRoom=world['startRoom'],startSpawn=world['startSpawn'],roomDirectory='rooms',prefabDirectory='prefabs'))
if __name__=='__main__':migrate(Path(sys.argv[1]),Path(sys.argv[2]))
