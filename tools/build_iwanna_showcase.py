"""Create editable Tiled showcase rooms and original demo art. Offline tool.
Rerunning overwrites the example maps. Runtime uses checked-in TMJ/Lua directly.
"""
from pathlib import Path
from PIL import Image,ImageDraw
import json
ROOT=Path(__file__).resolve().parents[1]/'Asset/IWanna'
SHOW=ROOT/'Showcase';ROOMS=SHOW/'rooms';EDITOR=ROOMS/'editor'
for p in (ROOMS,EDITOR,SHOW/'scripts'):p.mkdir(parents=True,exist_ok=True)
def write(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def art(name,im):im.resize((im.width*4,im.height*4),Image.Resampling.NEAREST).save(ROOT/'images'/name)
im=Image.new('RGBA',(1,1),'white');art('demo_white.png',im)
im=Image.new('RGBA',(32,32));d=ImageDraw.Draw(im)
d.polygon([(2,30),(16,1),(30,30)],fill='#203545');d.polygon([(5,28),(16,5),(26,28)],fill='#d9e9ee')
d.polygon([(16,5),(26,28),(16,28)],fill='#718da6');art('demo_spike.png',im)
im=Image.new('RGBA',(32,32));d=ImageDraw.Draw(im)
d.rectangle((15,1,17,8),fill='#6e513d');d.polygon([(17,4),(23,1),(26,3),(19,7)],fill='#86cf70')
d.ellipse((4,7,28,29),fill='#512b40');d.ellipse((6,8,26,27),fill='#df5863')
d.rectangle((9,11,12,15),fill='#ffb78f');d.line((10,25,22,25),fill='#a53251',width=2);art('demo_apple.png',im)
im=Image.new('RGBA',(32,48));d=ImageDraw.Draw(im)
d.rectangle((2,1,29,47),fill='#183245');d.rectangle((4,3,27,44),fill='#71cdd7')
d.rectangle((6,6,25,43),fill='#254862');d.line((7,7,24,7),fill='#b7f4e5')
d.polygon([(12,21),(19,27),(12,33)],fill='#abe7d1');d.rectangle((10,25,17,28),fill='#abe7d1')
d.rectangle((0,44,31,47),fill='#4e6c85');art('demo_door.png',im)
im=Image.new('RGBA',(32,32));d=ImageDraw.Draw(im)
d.ellipse((5,5,26,26),fill='#193e64');d.ellipse((8,8,23,23),fill='#72d6f3');d.rectangle((11,10,14,13),fill='#e3ffff');art('demo_orb.png',im)
im=Image.new('RGBA',(32,8));d=ImageDraw.Draw(im);d.rectangle((0,2,31,7),fill='#563346')
for x in range(0,32,8):d.polygon([(x,2),(x+4,2),(x,6),(x-4,6)],fill='#e29a65')
art('demo_sensor.png',im)

tiles=[]
for i in range(16):
    name=f'terrain_{i:02}.png';Image.open(ROOT/'images'/name).resize((32,32),Image.Resampling.NEAREST).save(EDITOR/name)
    tiles.append(dict(id=i,image=f'editor/{name}',imagewidth=32,imageheight=32,properties=[dict(name='runtimeImage',type='string',value=name)]))
write(ROOMS/'terrain.tsj',dict(type='tileset',version='1.10',tiledversion='1.11.2',name='Showcase stone',tilewidth=32,tileheight=32,tilecount=16,columns=0,grid=dict(orientation='orthogonal',width=1,height=1),tiles=tiles))

def props(values):
    return [dict(name=k,type='bool' if isinstance(v,bool) else 'int' if isinstance(v,int) else 'float' if isinstance(v,float) else 'string',value=v) for k,v in values.items()]
class Room:
    def __init__(self):self.grid=[[0]*60 for _ in range(34)];self.objects=[]
    def fill(self,x,y,w,h):
        for row in range(y,y+h):
            for col in range(x,x+w):self.grid[row][col]=1
    def rect(self,uid,prefab,x,y,w,h,**extra):
        self.objects.append(dict(id=len(self.objects)+1,name=uid,**{'class':'Entity'},x=(x-w/2+75)*12.8,y=(y-h/2+42.5)*12.8,width=w*12.8,height=h*12.8,rotation=0,visible=True,properties=props(dict(uid=uid,prefab=prefab,**extra))))
    def point(self,uid,x,y):self.objects.append(dict(id=len(self.objects)+1,name=uid,**{'class':'Spawn'},x=(x+75)*12.8,y=(y+42.5)*12.8,width=0,height=0,point=True,rotation=0,visible=True,properties=props(dict(uid=uid))))
    def label(self,text,x,y,scale=.3):self.objects.append(dict(id=len(self.objects)+1,name=text,**{'class':'Label'},x=(x+75)*12.8,y=(y+42.5)*12.8,width=0,height=0,point=True,rotation=0,visible=True,properties=props(dict(text=text,textScale=scale))))
    def save(self,name):
        def filled(x,y):return 0<=x<60 and 0<=y<34 and self.grid[y][x]
        data=[]
        for y in range(34):
            for x in range(60):
                edges=(not filled(x,y-1))*1+(not filled(x+1,y))*2+(not filled(x,y+1))*4+(not filled(x-1,y))*8
                data.append(edges+1 if filled(x,y) else 0)
        write(ROOMS/f'{name}.tmj',dict(type='map',version='1.10',tiledversion='1.11.2',orientation='orthogonal',renderorder='right-down',infinite=False,width=60,height=34,tilewidth=32,tileheight=32,nextlayerid=3,nextobjectid=len(self.objects)+1,
            properties=props(dict(worldTileSize=2.5,originX=-75.,originY=-42.5)),tilesets=[dict(firstgid=1,source='terrain.tsj')],
            layers=[dict(id=1,name='Terrain',type='tilelayer',x=0,y=0,width=60,height=34,opacity=1,visible=True,data=data),dict(id=2,name='Objects',type='objectgroup',draworder='topdown',opacity=1,visible=True,x=0,y=0,objects=self.objects)]))

movement=Room();movement.fill(0,26,60,8)
movement.fill(19,25,3,1);movement.fill(23,24,3,2);movement.fill(27,23,3,3)
movement.fill(35,22,2,4)
movement.point('left',-61,20.7);movement.point('right',57,20.7)
movement.rect('checkpoint','checkpoint',-49,20.1,4,4.8)
movement.rect('to_traps','door',67,17.5,5,10,destinationRoom='traps',destinationSpawn='left')
movement.label('FLAT JOINTS',-67,12);movement.label('SMALL STEPS',-28,9)
movement.label('WALL SLIDE',10,-5);movement.label('NEXT ROOM',49,3)
movement.save('movement')

traps=Room();traps.fill(0,26,30,8);traps.fill(34,26,26,8);traps.fill(30,32,4,2)
traps.point('left',-58,20.7);traps.point('right',58,20.7)
traps.rect('to_movement','door',-68,17.5,5,10,destinationRoom='movement',destinationSpawn='right')
traps.rect('to_gallery','door',68,17.5,5,10,destinationRoom='gallery',destinationSpawn='left')
traps.rect('apple_sensor','trigger',-37,19,12,7,event='drop_apple')
traps.rect('apple_01','apple',-33,0,3,3)
traps.rect('sensor_mark','sensor',-37,22.1,12,.8)
traps.rect('checkpoint','checkpoint',-15,20.1,4,4.8)
traps.rect('bridge','platform',5,23.75,10,2.5,event='bridge_contact')
for i in range(4):traps.rect(f'pit_spike_{i}','spike',1.25+i*2.5,36.25,2.5,2.5)
traps.rect('spike_01','spike',35,21.25,2.5,2.5)
traps.rect('spike_02','spike',37.5,21.25,2.5,2.5)
traps.label('1 FALLING APPLE',-55,-8);traps.label('SAVE',-20,10)
traps.label('2 DELAYED BRIDGE',-3,-8);traps.label('3 SPIKES',28,8)
traps.save('traps')

gallery=Room();gallery.fill(0,26,60,8);gallery.point('left',-58,20.7)
gallery.rect('to_traps','door',-68,17.5,5,10,destinationRoom='traps',destinationSpawn='right')
gallery.rect('loop','door',68,17.5,5,10,destinationRoom='movement',destinationSpawn='left')
gallery.rect('checkpoint','checkpoint',-45,20.1,4,4.8)
gallery.rect('spawn_sensor','trigger',-15,19,8,7,event='spawn_demo')
gallery.rect('spawn_mark','sensor',-15,22.1,8,.8)
gallery.rect('finish','trigger',35,19,8,7,event='tour_finish')
gallery.rect('finish_mark','sensor',35,22.1,8,.8)
gallery.label('PERSISTENT CHECKPOINT',-57,7,.24);gallery.label('SPAWN AND DESTROY',-29,-5,.28)
gallery.label('TOUR FINISH',22,8);gallery.label('LOOP BACK',52,3)
gallery.save('gallery')

prefabs={
 'platform':dict(role='solid',image='terrain_01.png',width=2.5,height=2.5),
 'apple':dict(role='hazard',image='demo_apple.png',width=3,height=3),
 'spike':dict(role='hazard',image='demo_spike.png',width=2.5,height=2.5),
 'door':dict(role='exit',image='demo_door.png',width=5,height=10),
 'checkpoint':dict(role='checkpoint',image='checkpoint_idle.png',width=4,height=4.8),
 'trigger':dict(role='trigger',image='demo_white.png',width=5,height=5,visible=False),
 'sensor':dict(role='decoration',image='demo_sensor.png',width=5,height=.8),
 'orb':dict(role='decoration',image='demo_orb.png',width=3,height=3)}
write(SHOW/'world.json',dict(format='IWANNA_WORLD_1',startRoom='movement',startSpawn='left',prefabs=prefabs,rooms=[
 dict(id='movement',map='rooms/movement.tmj',script='scripts/movement.lua',title='01  MOVEMENT LAB',hint='WALK THE JOINTS   JUMP THE STEPS   USE THE DOOR'),
 dict(id='traps',map='rooms/traps.tmj',script='scripts/traps.lua',title='02  TRAP LAB',hint='WATCH THE SENSOR   SAVE FIRST   R RESETS ALL TRAPS'),
 dict(id='gallery',map='rooms/gallery.tmj',script='scripts/gallery.lua',title='03  EVENT GALLERY',hint='TRIGGER AN ORB   WATCH IT EXPIRE   LOOP BACK TO ROOM 01')]))
write(SHOW/'showcase.tiled-project',dict(folders=['rooms'],propertyTypes=[]))
print('Wrote three connected Tiled rooms, tileset, world and demo art')
