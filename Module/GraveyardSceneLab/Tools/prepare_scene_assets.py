"""Package a selected local GK2 extraction for the OpenGL study.

No engine archives are modified. Mesh positions, UVs, normals and vertex colour
metadata survive; submesh textures are packed into a new atlas without filtering.
Our map's +Z is Unity -Z. Selection/coordinate/atlas provenance is saved as JSON.
"""
from pathlib import Path
import argparse, json, shutil, struct, hashlib
import numpy as np
from PIL import Image

ap=argparse.ArgumentParser()
ap.add_argument('--extraction',type=Path,default=Path('E:/Asset/GraveyardKeeper2Scene'))
ap.add_argument('--module',type=Path,default=Path(__file__).resolve().parents[1])
args=ap.parse_args();src=args.extraction;dst=args.module/'Assets'
(dst/'textures').mkdir(parents=True,exist_ok=True);(dst/'meshes').mkdir(exist_ok=True)
manifests=[json.loads((src/p).read_text(encoding='utf-8')) for p in ['manifest.json','manifest-outdoor.json','manifest-details.json'] if (src/p).exists()]
textures={x['pathID']:x for m in manifests for x in m['textures']}
meshes={x['pathID']:x for m in manifests for x in m['meshes']}
byname={x['name']:x for x in meshes.values()}
nodes={(x['cab'],x['pathID']):x for m in manifests for x in m['nodes']}
go={k:v for k,v in nodes.items() if v['type']=='GameObject'}
transforms={k:v for k,v in nodes.items() if v['type']=='Transform'}
go_transform={(v['cab'],v['data']['m_GameObject']['m_PathID']):k for k,v in transforms.items()}
filters={(v['cab'],v['data']['m_GameObject']['m_PathID']):v for v in nodes.values() if v['type']=='MeshFilter'}
bindings={}
for n in nodes.values():
    d=n['data']
    if n['type']=='MonoBehaviour' and 'subMeshes' in d:
        key=(n['cab'],d['m_GameObject']['m_PathID'])
        if key in filters:
            bindings[key]=[s['texture']['m_PathID'] for s in d['subMeshes']]

def matrix(key):
    if key not in transforms:return np.eye(4)
    d=transforms[key]['data'];r=d['m_LocalRotation'];x,y,z,w=[r[k] for k in 'xyzw']
    rot=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                  [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                  [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    mat=np.eye(4);mat[:3,:3]=rot@np.diag([d['m_LocalScale'][k] for k in 'xyz'])
    mat[:3,3]=[d['m_LocalPosition'][k] for k in 'xyz']
    parent=(key[0],d['m_Father']['m_PathID'])
    return matrix(parent)@mat if parent in transforms else mat

def root_name(gokey):
    key=go_transform.get(gokey)
    if key is None:return ''
    while transforms[key]['data']['m_Father']['m_PathID']:
        parent=(key[0],transforms[key]['data']['m_Father']['m_PathID'])
        if parent not in transforms:break
        key=parent
    return go.get((key[0],transforms[key]['data']['m_GameObject']['m_PathID']),{}).get('data',{}).get('m_Name','')

assets=[];modelrows=[];evidence=[]
def texture(role,name,pivot=(.5,0),crop=None):
    t=next(t for t in textures.values() if t['name']==name)
    im=Image.open(src/t['png']).convert('RGBA')
    if crop:im=im.crop(crop)
    file=f'textures/{role}.png';im.save(dst/file)
    assets.append((role,file,*pivot));evidence.append({'role':role,'source_texture':t,'derived_crop':crop})

texture('ground','land_grass_green_C-tile')
texture('dirt','ter-grass_dirt-clay_spot',crop=(68,68,124,124))
texture('leaf','falling_leaf_1_at',crop=(0,0,10,10))
for index in range(1,8):texture(f'leaf{index}','falling_leaf_1_at',crop=(index*10,0,index*10+10,10))
for role,name in [('water-noise','water_surface_map_1_sm.png'),('foam-noise','voronoi_map_5.png')]:
    source=src/'water'/name
    if source.exists():
        file=f'textures/{role}.png';shutil.copyfile(source,dst/file);assets.append((role,file,.5,0))

def collect_raw(meshname,texture_names=None):
    mesh=byname[meshname]
    if texture_names:
        tids=[next(t['pathID'] for t in textures.values() if t['name']==name) for name in texture_names]
    else:
        candidates=[(k,v) for k,v in filters.items() if v['data']['m_Mesh']['m_PathID']==mesh['pathID'] and k in bindings]
        if not candidates:raise ValueError(f'No texture-property-block binding: {meshname}')
        tids=bindings[candidates[0][0]]
    return [(mesh,tids,np.eye(4))]

def collect_prefab(root):
    selected=[]
    for key,f in filters.items():
        if root_name(key)!=root or key not in bindings:continue
        mid=f['data']['m_Mesh']['m_PathID']
        if mid not in meshes:continue
        mesh=meshes[mid]
        if any(q in mesh['name'].lower() for q in ['shadow','_sh2','blackout','collision']):continue
        tids=bindings[key]
        if not all(tid in textures for tid in tids):continue
        selected.append((mesh,tids,matrix(go_transform[key])))
    if not selected:raise ValueError(f'No render geometry in prefab {root}')
    return selected

def model(role,parts,normalize_support=True):
    tids=list(dict.fromkeys(tid for _,tt,_ in parts for tid in tt))
    ims={tid:Image.open(src/textures[tid]['png']).convert('RGBA') for tid in tids}
    # One scanline atlas is enough for these small, selected prop sets.
    aw=sum(im.width+4 for im in ims.values())+4;ah=max(im.height for im in ims.values())+8
    atlas=Image.new('RGBA',(aw,ah));offset={};x=4
    for tid,im in ims.items():atlas.paste(im,(x,4));offset[tid]=(x,4);x+=im.width+4
    file=f'textures/{role}.png';atlas.save(dst/file);assets.append((role,file,.5,0))
    vertices=[];mesh_evidence=[]
    for mesh,tt,mat in parts:
        d=json.loads((src/mesh['json']).read_text(encoding='utf-8'))
        normals=np.linalg.inv(mat[:3,:3]).T
        for si,triangles in enumerate(d['submeshes']):
            tid=tt[min(si,len(tt)-1)];im=ims[tid];ox,oy=offset[tid]
            for triangle in triangles:
                for index in triangle:
                    p=mat@np.array([*d['vertices'][index],1.]);n=normals@np.array(d['normals'][index][:3] if d['normals'] else [0,1,0]);n=n/max(np.linalg.norm(n),1e-8)
                    uv=d['uv'][index];colour=d['colors'][index] if d['colors'] else [255]*4
                    if max(colour)>1:colour=[q/255 for q in colour]
                    vertices.append([p[0],p[1],-p[2],(ox+uv[0]*im.width)/aw,(oy+(1-uv[1])*im.height)/ah,n[0],n[1],-n[2],*colour])
        mesh_evidence.append({'source_mesh':mesh,'source_textures':[textures[t] for t in tt],'transform':mat.tolist()})
    va=np.asarray(vertices,dtype=np.float32)
    if normalize_support:
        low=va[:,1].min();foot=va[va[:,1]<low+.04]
        anchor=float(np.median(foot[:,2]));va[:,1]-=low;va[:,2]-=anchor
    else:anchor=0
    va[:,0]-=(va[:,0].min()+va[:,0].max())*.5
    meshfile=f'meshes/{role}.bin'
    with (dst/meshfile).open('wb') as f:f.write(struct.pack('<I',len(va)));f.write(va.astype('<f4').tobytes())
    modelrows.append((role,meshfile));evidence.append({'role':role,'parts':mesh_evidence,'vertex_count':len(va),'coordinate_adaptation':'X,Y,-Z; PNG UV V inverted','support_anchor_z':anchor,'atlas_sha256':hashlib.sha256((dst/file).read_bytes()).hexdigest()})
    print(role,'vertices',len(va),'atlas',atlas.size)

model('tree',collect_raw('tree_linden_clr1_l-MERGE A_OBJ',['_vegetation_atlas_set_1']))
model('tree2',collect_raw('tree_birch_clr1_m_v1-MERGE A_OBJ',['_vegetation_atlas_set_1']))
model('bush',collect_raw('bush_ordinary_clr1_m_v1-MERGE A_OBJ',['_vegetation_atlas_set_1']))
model('rock',collect_raw('rock_m_blue-wall-strght-f-m',['rock_m_blue-wall-strght-f-m-prt1']))
model('house',collect_prefab('church_t1'),normalize_support=False)
model('grave',collect_prefab('grave_top_stn_1'))
model('grave2',collect_prefab('grave_top_stn_3'))
model('gravebase',collect_prefab('grave_bot_stn_1'))
model('bridge',collect_prefab('bridge_vineyard'),normalize_support=False)
model('lantern',collect_raw('light_source_lamp_post_wood_m_v1'))
model('flowerbed',collect_raw('grave_flowerbed_wood_t2_cnd1-MERGE S_OBJ'))
# Source plant_02 has its per-submesh textures on renderer materials. The final
# extraction also carries ordinary plant images; use the actual flowerbed as a
# small roadside accent until a dedicated grass asset is present.
texture('grass','deforming_grass',crop=(24,0,48,64))
texture('grass2','deforming_grass',crop=(24*3,64,24*4,128))
texture('reeds','deforming_grass',crop=(24*7,0,24*8,64))
texture('flowers','deforming_grass',crop=(24*9,0,24*10,64))
texture('cliff','rock_m_blue-wall-strght-f-m-prt1')

player_paths=[src/f'character/keeper_walk_{d}_{fr:02}.png' for d in ['down','up','right','left'] for fr in [1,3,5,7]]
player_boxes=[Image.open(p).getbbox() for p in player_paths if p.exists()]
player_union=(min(b[0] for b in player_boxes),min(b[1] for b in player_boxes),max(b[2] for b in player_boxes),max(b[3] for b in player_boxes))
for direction in ['down','up','right','left']:
    for index,frame in enumerate([1,3,5,7]):
        p=src/f'character/keeper_walk_{direction}_{frame:02}.png'
        if p.exists():
            im=Image.open(p).convert('RGBA').crop(player_union)
            file=f'textures/player_{direction}_{index}.png';im.save(dst/file)
            # Keep one shared canvas/scale for all frames, with the original
            # canvas anchor (48,65) at the feet. Per-frame bbox rescaling would
            # make narrow side poses change size while walking.
            assets.append((f'player_{direction}_{index}',file,(48-player_union[0])/im.width,(player_union[3]-65)/im.height))

(dst/'scene-assets.tsv').write_text('# role\tPNG relative to Assets\tpivot normalized X\tpivot normalized Y\n'+'\n'.join('\t'.join(map(str,row)) for row in assets)+'\n',encoding='utf-8')
(dst/'scene-meshes.tsv').write_text('# role\tmesh binary: uint32 count + float12 position/UV/normal/RGBA metadata\n'+'\n'.join('\t'.join(row) for row in modelrows)+'\n',encoding='utf-8')
(dst/'provenance.json').write_text(json.dumps({'source':str(src),'models':evidence},ensure_ascii=False,indent=2),encoding='utf-8')
print('Packaged',len(assets),'texture roles and',len(modelrows),'original mesh models')
