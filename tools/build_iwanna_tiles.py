"""Offline migration and original pixel-art generation (Pillow only).
Runtime reads tilemap.json; never needs Python or the legacy engine.
"""
from pathlib import Path
from PIL import Image, ImageDraw
import shlex, math, json, hashlib, random

ROOT=Path(__file__).resolve().parents[1]/'Asset/IWanna'
OUT=ROOT/'images'
rows=[shlex.split(line) for line in (ROOT/'level.txt').read_text().splitlines()[1:] if line]
images={r[1]:Image.open(OUT/r[1]).convert('RGBA') for r in rows}
def bounds(r):
    im=images[r[1]]; box=im.getbbox(); x,y,w,h=map(float,r[3:7]); angle=math.radians(float(r[7]))
    pts=[]
    for px,py in [(box[0],box[1]),(box[2],box[1]),(box[2],box[3]),(box[0],box[3])]:
        u=(px/im.width-.5)*w;v=(py/im.height-.5)*h
        if r[9]=='1':u=-u
        if r[10]=='1':v=-v
        pts.append((x+u*math.cos(angle)-v*math.sin(angle),y+u*math.sin(angle)+v*math.cos(angle)))
    return min(p[0] for p in pts),min(p[1] for p in pts),max(p[0] for p in pts),max(p[1] for p in pts)

def save(im,name):
    # Integer nearest-neighbor scaling preserves designed pixels, no blurry upsampling.
    im.resize((im.width*4,im.height*4),Image.Resampling.NEAREST).save(OUT/name)

# 16 exposed-edge variants. All terrain fills the cell, including its collision.
for edges in range(16):
    im=Image.new('RGBA',(32,32),'#313848');d=ImageDraw.Draw(im);rng=random.Random(8)
    for row in range(4):
        for col in range(-1,3):
            x=col*16+(8 if row%2 else 0);y=row*8
            base=['#626d80','#566276','#667183','#5c687b'][(row+col)%4]
            d.rectangle((x+1,y+1,x+15,y+7),fill=base)
            d.line((x+2,y+1,x+14,y+1),fill='#8994a2')
            d.line((x+1,y+7,x+15,y+7),fill='#414d62')
            d.point((x+rng.randrange(3,14),y+4),fill='#778498')
    if edges&2:d.rectangle((30,0,31,31),fill='#273043');d.line((29,0,29,31),fill='#47566b')
    if edges&4:d.rectangle((0,30,31,31),fill='#242e40')
    if edges&8:d.line((0,0,0,31),fill='#303d50');d.line((1,0,1,31),fill='#8994a2')
    if edges&1:
        d.rectangle((0,0,31,4),fill='#4f913b');d.line((0,0,31,0),fill='#b5d77a')
        d.line((0,1,31,1),fill='#8dbb55')
        for x in range(1,32,4):
            d.rectangle((x,3,x+1,5+(x%3)),fill='#396b38')
            d.point((x+1,1),fill='#d0e898')
    save(im,f'terrain_{edges:02}.png')

# Wooden signs with readable pictograms and metal corner fasteners.
im=Image.new('RGBA',(40,48));d=ImageDraw.Draw(im)
d.rectangle((17,24,23,47),fill='#322f37');d.rectangle((19,25,21,47),fill='#986c4a')
d.rectangle((1,1,38,29),fill='#242a38');d.rectangle((3,3,36,27),fill='#a7764b')
d.rectangle((5,5,34,25),fill='#e2c38a');d.line((5,5,34,5),fill='#fff0b9')
for y in (12,20):d.line((5,y,34,y),fill='#cfad75')
for x,y in [(4,4),(35,4),(4,26),(35,26)]:d.rectangle((x,y,x+1,y+1),fill='#728694')
d.rectangle((10,13,24,16),fill='#564634');d.polygon([(23,9),(30,15),(23,21)],fill='#564634')
save(im,'sign_pixel.png')
for active in (False,True):
    im=Image.new('RGBA',(40,48));d=ImageDraw.Draw(im)
    d.rectangle((4,39,35,47),fill='#222c40');d.rectangle((7,38,32,43),fill='#697c92')
    d.line((8,38,31,38),fill='#c0d0d7');d.rectangle((11,33,28,37),fill='#3d526d')
    d.polygon([(20,1),(33,15),(26,31),(14,31),(7,15)],fill='#203346')
    mid,light,dark=('#54dca1','#dbffe6','#249a87') if active else ('#66b8e5','#d1f3ff','#3b67ae')
    d.polygon([(20,4),(29,15),(24,28),(16,28),(11,15)],fill=mid)
    d.polygon([(20,4),(20,27),(11,15)],fill=dark)
    d.polygon([(20,4),(29,15),(21,12)],fill=light)
    d.line((20,7,20,24),fill=light,width=2)
    if active:
        for x,y in [(4,8),(33,5),(35,27)]:d.line((x-2,y,x+2,y),fill=light);d.line((x,y-2,x,y+2),fill=light)
    save(im,'checkpoint_active.png' if active else 'checkpoint_idle.png')

size=2.5;ox,oy=-100.,-45.;width,height=80,36
solids=[(i,r,bounds(r)) for i,r in enumerate(rows) if r[2]=='solid']
grid=[[0]*width for _ in range(height)]
for y in range(height):
    for x in range(width):
        cx=ox+(x+.5)*size;cy=oy+(y+.5)*size
        if any(a<=cx<b and c<=cy<d for _,_,(a,c,b,d) in solids):grid[y][x]=1
markers=[]
for _,r,b in solids:
    if r[-3]:
        cx=(b[0]+b[2])/2;cy=b[1]+size/2
        candidates=[(abs(ox+(x+.5)*size-cx)+abs(oy+(y+.5)*size-cy),x,y) for y in range(height) for x in range(width) if grid[y][x]]
        _,x,y=min(candidates);markers.append(dict(column=x,row=y,name=r[0],target=r[-3],velocity=[float(r[-2]),float(r[-1])]))

def floor_near(x,old_bottom):
    col=max(0,min(width-1,int((x-ox)/size)))
    tops=[oy+y*size for y in range(height) if grid[y][col] and (y==0 or not grid[y-1][col])]
    return min(tops,key=lambda top:abs(top-old_bottom)) if tops else old_bottom

surfaces=[]
for gy in range(height):
    for gx in range(width):
        if not grid[gy][gx]:continue
        left=ox+gx*size;top=oy+gy*size
        for face,dx,dy,axis,edge,a,b in [('top',0,-1,1,top,left,left+size),('bottom',0,1,1,top+size,left,left+size),('left',-1,0,0,left,top,top+size),('right',1,0,0,left+size,top,top+size)]:
            nx,ny=gx+dx,gy+dy
            if not(0<=nx<width and 0<=ny<height and grid[ny][nx]):surfaces.append((face,axis,edge,a,b))

objects=[]
for index,r in enumerate(rows):
    if r[2]=='solid':continue
    x,y,w,h=map(float,r[3:7]);b=bounds(r)
    if r[1] in ('pai.png','keep.png','unkeep.png'):
        name={'pai.png':'sign_pixel.png','keep.png':'checkpoint_active.png','unkeep.png':'checkpoint_idle.png'}[r[1]]
        w,h=4.,4.8
        candidates=[(3*abs(edge-b[3])+abs((a+z)/2-x),(a+z)/2,edge) for face,axis,edge,a,z in surfaces if face=='top' and abs((a+z)/2-x)<6.25]
        _,x,floor=min(candidates);y=floor-h/2
        objects.append(dict(sourceIndex=index,image=name,position=[x,y],size=[w,h],pixelArt=True))
    elif r[2]=='hazard' and r[1]=='ci.png':
        # Move the spike with its nearest original support face, keeping shape/rotation.
        choices=[]
        for _,_,s in solids:
            if min(b[2],s[2])>max(b[0],s[0]):
                choices += [(abs(b[3]-s[1]),1,s[1],'top',b[3]),(abs(b[1]-s[3]),1,s[3],'bottom',b[1])]
            if min(b[3],s[3])>max(b[1],s[1]):
                choices += [(abs(b[2]-s[0]),0,s[0],'left',b[2]),(abs(b[0]-s[2]),0,s[2],'right',b[0])]
        if choices:
            distance,axis,edge,face,hazard_edge=min(choices)
            if distance<1.5:
                lo,hi=(b[0],b[2]) if axis==1 else (b[1],b[3])
                candidates=[(abs(newedge-hazard_edge)+.1*abs((a+z-lo-hi)/2),newedge) for f,ax,newedge,a,z in surfaces if f==face and abs(newedge-edge)<=2.5 and min(z,hi)-max(a,lo)>.25]
                if candidates:
                    _,newedge=min(candidates);pos=[x,y];pos[axis]+=newedge-hazard_edge
                    objects.append(dict(sourceIndex=index,position=pos))
    elif r[2]=='vanish':
        w=max(size,round(w/size)*size);h=size;x=ox+(round((x-ox)/size-.5)+.5)*size;y=oy+(round((y-oy)/size-.5)+.5)*size
        objects.append(dict(sourceIndex=index,image='terrain_15.png',position=[x,y],size=[w,h],pixelArt=True,repeatX=int(w/size)))

player=next(r for r in rows if r[2]=='player');px=float(player[3]);spawn=[px,floor_near(px,float(player[4])+1.5)-1.6]
data=dict(format='IWANNA_TILEMAP_1',tileSize=size,origin=[ox,oy],width=width,height=height,
          palette={'1':{'name':'stone','imagePattern':'terrain_{edges:02}.png','solid':True}},
          layers=[dict(name='terrain',cells=grid)],markers=markers,objects=objects,spawn=spawn)
(ROOT/'tilemap.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
# Compact text map is the hand-editable representation: each character is a tile ID.
data['layers'][0]['cells']=[''.join(str(n) for n in row) for row in grid]
(ROOT/'tilemap.json').write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
art=[OUT/f'terrain_{i:02}.png' for i in range(16)]+[OUT/n for n in ('sign_pixel.png','checkpoint_idle.png','checkpoint_active.png')]
(ROOT/'tile_art_manifest.json').write_text(json.dumps(dict(author='Original procedural pixel art generated for this project',generator='tools/build_iwanna_tiles.py',sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in art}),indent=2)+'\n')
print('Terrain cells:',sum(map(sum,grid)),'markers:',len(markers),'object adjustments:',len(objects),'spawn:',spawn)
