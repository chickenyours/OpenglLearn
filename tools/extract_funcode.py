"""Extract source/data only; never load or execute the legacy engine DLLs.
Usage: python tools/extract_funcode.py <funcode-root> [output=Asset/IWanna]
The checked-in output is sufficient for building/running without Python.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

source = Path(sys.argv[1])
output = Path(sys.argv[2] if len(sys.argv) > 2 else 'Asset/IWanna')
output.mkdir(parents=True, exist_ok=True)
game = source / 'Bin/game'

def blocks(path):
    text = path.read_text(encoding='utf-8', errors='replace')
    for kind, name, body in re.findall(r'new\s+(\w+)\(([^)]*)\)\s*\{([^{}]*)\};', text):
        yield kind, name, dict(re.findall(r'(\w+)\s*=\s*"([^"\r\n]*)";', body))

db = {name: props for _, name, props in blocks(game / 'managed/datablocks.cs')}
objects = list(blocks(game / 'data/levels/level.t2d'))
records, images = [], set()
counts = collections.Counter()
for index, (kind, name, p) in enumerate(objects):
    anim = db.get(p.get('animationName', ''), {})
    image = db.get(p.get('imageMap', anim.get('imageMap', '')), {})
    filename = Path(image.get('imageName', '')).name
    # Old unconfigured man and its mounted helper are editor leftovers. The
    # final animated man (mountID 218) is the playable object used by the code.
    if not filename:
        continue
    images.add(filename)
    role = 'decoration'
    if name == 'man': role = 'player'
    elif name == 'move': role = 'run_template'
    elif name == 'x': role = 'death_template'
    elif name.startswith('ci'): role = 'hazard'
    elif name.startswith('unkeep'): role = 'checkpoint'
    elif name == 'gg': role = 'goal'
    elif name.startswith('tu'): role = 'vanish'
    elif name.startswith(('cao', 'di')): role = 'solid'
    elif name.startswith('start'): role = 'intro'
    elif name.startswith('end'): role = 'ending'
    counts[role] += 1
    collision = p.get('CollisionPolyList', '-1 -1 1 -1 1 1 -1 1').split()
    fields = [json.dumps(name or f'decor_{index}'), json.dumps(filename), json.dumps(role),
              p.get('Position','0 0'), p.get('size','1 1'), p.get('Rotation','0'), p.get('Layer','0'),
              p.get('FlipX','0'), p.get('FlipY','0'), p.get('CollisionActiveReceive','0'),
              str(max(1,int(image.get('cellCountX','1')))), str(max(1,int(image.get('cellCountY','1')))),
              image.get('cellWidth','0'), image.get('cellHeight','0'), anim.get('animationTime','1'),
              json.dumps(anim.get('animationFrames',p.get('frame','0'))),
              str(len(collision)//2), ' '.join(collision)]
    target, velocity = {'cao1': ('ciapple1', '-100 0'), 'cao2': ('ciapple2', '0 50'),
                        'cao3': ('ciapple3', '0 50')}.get(name, ('', '0 0'))
    fields += [json.dumps(target), velocity]
    records.append(' '.join(fields))
(output/'level.txt').write_text('IWANNA_LEVEL_1\n'+'\n'.join(records)+'\n', encoding='utf-8')
for filename in sorted(images):
    (output/'images').mkdir(exist_ok=True)
    shutil.copy2(game/'data/images'/filename, output/'images'/filename)
for path in (game/'data/audio').glob('*.wav'):
    (output/'audio').mkdir(exist_ok=True)
    shutil.copy2(path, output/'audio'/path.name)
for path in [source/'SourceCode/Src/Main.cpp', source/'SourceCode/Src/LessonX.cpp',
             source/'SourceCode/Header/LessonX.h', source/'SourceCode/Header/CommonAPI.h',
             game/'data/levels/level.t2d', game/'managed/datablocks.cs']:
    (output/'reference').mkdir(exist_ok=True)
    shutil.copy2(path, output/'reference'/path.name)
manifest = {'source': source.name, 'objects': len(records), 'roles': dict(counts), 'images': sorted(images),
            'sha256': {str(p.relative_to(output)).replace('\\','/'): hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'manifest.json'}}
(output/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'objects':len(records), 'images':len(images), 'roles':dict(counts)}, ensure_ascii=False))
