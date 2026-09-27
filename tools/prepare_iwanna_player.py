"""Deterministic import of the user-supplied sprite sheet, no resampling.
White connected to each crop boundary becomes alpha=0; enclosed white details
are preserved. Run with the bundled Python/Pillow, only when changing assets.
"""
from pathlib import Path
from collections import deque
import json
import hashlib
import sys
from PIL import Image

root = Path(__file__).resolve().parents[1] / 'Asset/IWanna'
source = Path(sys.argv[1]) if len(sys.argv)>1 else root/'player/source.png'
im = Image.open(source).convert('RGBA')
(root/'player').mkdir(exist_ok=True)
if source.resolve() != (root/'player/source.png').resolve():
    (root/'player/source.png').write_bytes(source.read_bytes())
clips = {
    'idle': [(0,0,108,92,45),(110,0,218,92,48),(222,0,332,92,50),(334,0,444,92,45)],
    'run': [(0,94,106,188,43),(108,94,212,188,43),(213,94,314,188,43),(315,94,414,188,43),(414,94,512,188,43)],
    'jump': [(0,209,110,299,43),(110,209,215,299,43)]
}
for name, crops in clips.items():
    sheet=Image.new('RGBA',(128*len(crops),96))
    for index,(left,top,right,bottom,anchor) in enumerate(crops):
        frame=im.crop((left,top,right,bottom))
        w,h=frame.size; pixels=frame.load(); queue=deque()
        seen=set()
        def seed(x,y):
            if (x,y) not in seen and min(pixels[x,y][:3])>=245:
                seen.add((x,y));queue.append((x,y))
        for x in range(w): seed(x,0);seed(x,h-1)
        for y in range(h): seed(0,y);seed(w-1,y)
        while queue:
            x,y=queue.popleft();pixels[x,y]=(0,0,0,0)
            for nx,ny in [(x-1,y),(x+1,y),(x,y-1),(x,y+1)]:
                if 0<=nx<w and 0<=ny<h: seed(nx,ny)
        bounds=frame.getbbox()
        if not bounds: raise ValueError('Empty frame')
        if bounds[0]+64-anchor<0 or bounds[2]+64-anchor>128 or bounds[3]-bounds[1]>88:
            raise ValueError(f'{name} frame {index} exceeds its cell')
        # Fixed body anchor and common foot baseline keep animation from hopping.
        sheet.alpha_composite(frame,(index*128+64-anchor,88-bounds[3]))
    sheet.save(root/'images'/f'player_{name}.png')
    alpha=sheet.getchannel('A').point(lambda a:255 if a>=128 else 0)
    (root/'masks').mkdir(exist_ok=True)
    alpha.save(root/'masks'/f'player_{name}.png')
    print(name,sheet.size)
(root/'player/crops.json').write_text(json.dumps(clips,indent=2))
files=[root/'player/source.png',root/'player/crops.json']
files += [root/'images'/f'player_{name}.png' for name in clips]
(root/'player/manifest.json').write_text(json.dumps({
    'source': 'User-supplied sprite sheet; original attribution retained in source.png',
    'sha256': {str(p.relative_to(root)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
},indent=2))
