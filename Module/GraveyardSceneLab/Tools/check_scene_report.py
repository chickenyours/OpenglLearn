from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit,unquote
from PIL import Image,ImageChops
import json,hashlib
MOD=Path(__file__).resolve().parents[1]
class Doc(HTMLParser):
 def __init__(self):super().__init__();self.refs=[];self.ids=[];self.images=[]
 def handle_starttag(self,t,attrs):
  a=dict(attrs)
  if 'id' in a:self.ids.append(a['id'])
  self.refs.extend(a[k] for k in ['src','href'] if k in a)
  if t=='img':self.images.append(a)
errors=[];docs={};images={};identical=[];animations={}
for name in ['index.html','lighting-v2.html','foundation.html']:
 p=MOD/'Report'/name;d=Doc();d.feed(p.read_text(encoding='utf8'))
 for ref in d.refs:
  u=urlsplit(ref)
  if u.scheme:continue
  if not u.path:
   if u.fragment and u.fragment not in d.ids:errors.append(name+': missing anchor '+ref)
  elif not (p.parent/unquote(u.path)).resolve().exists():errors.append(name+': missing local path '+ref)
 if len(d.ids)!=len(set(d.ids)):errors.append(name+': duplicate IDs')
 for a in d.images:
  if not a.get('alt'):errors.append(name+': image missing alt')
  if urlsplit(a['src']).scheme:continue
  q=p.parent/a['src']
  if not q.exists():continue
  with Image.open(q) as im:im.verify()
  with Image.open(q) as im:
   frames=getattr(im,'n_frames',1);images[str(q.relative_to(MOD))]={'size':im.size,'frames':frames}
   for i in range(frames):im.seek(i);im.load()
 docs[name]={'references':len(d.refs),'imageOccurrences':len(d.images),'anchors':len(d.ids)}
for p in sorted((MOD/'Captures/lighting-v3').glob('*.png')):
 q=MOD/'Report/images/scene-v3'/(p.stem+'.webp')
 a=Image.open(p).convert('RGB');b=Image.open(q).convert('RGB')
 same=a.size==b.size and ImageChops.difference(a,b).getbbox() is None
 if not same:errors.append('Report differs from actual GPU capture '+p.name)
 identical.append({'capture':p.name,'rgbPixelIdentical':same,'sourceSha256':hashlib.sha256(p.read_bytes()).hexdigest()})
for view in ['water','scene']:
 src=sorted((MOD/'Captures/motion-v3'/view).glob('frame-*.png'))
 p=MOD/'Report/images/scene-v3'/(view+'-motion.webp')
 with Image.open(p) as im:
  count=getattr(im,'n_frames',1);all_same=count==len(src)
  for i,frame in enumerate(src):
   if i>=count:all_same=False;break
   im.seek(i);a=im.convert('RGB');b=Image.open(frame).convert('RGB').resize((960,540),Image.Resampling.NEAREST)
   if ImageChops.difference(a,b).getbbox() is not None:all_same=False
  im.seek(0);first=im.convert('RGB');im.seek(count-1)
  diff=ImageChops.difference(first,im.convert('RGB'))
  changed=sum(1 for px in diff.getdata() if max(px)>3)
  animations[view]={'decodedFrames':count,'allFramesPixelIdenticalToGPUAtNearest960x540':all_same,'firstLastChangedPixelsAbove3':changed}
  if not all_same or count!=24 or changed<30:errors.append('Motion image invalid '+view)
pairs={}
for kind,a,b in [('sun-shadow','sun-shadow-off','sun-shadow-on'),('PCF','pcf-off','pcf-on'),('AO','contact-ao-off','contact-ao-on'),('cloud','clouds-off','clouds-on'),('water-depth','water-depth-off','water-depth-on'),('water-refraction','water-refraction-off','water-refraction-on'),('water-reflection','water-reflection-off','water-reflection-on')]:
 diff=ImageChops.difference(Image.open(MOD/'Captures/lighting-v3'/(a+'.png')).convert('RGB'),Image.open(MOD/'Captures/lighting-v3'/(b+'.png')).convert('RGB'))
 changed=sum(1 for px in diff.getdata() if max(px)>3)
 pairs[kind]={'changedPixelsAbove3':changed,'a':a+'.png','b':b+'.png'}
 if changed<30:errors.append('Effect pair lacks visible GPU difference '+kind)
temporal=json.loads((MOD/'Captures/stability-v3/temporal-validation.json').read_text())
for field in ['frozen_changed_pixels_max','opaque_model_and_terrain_order_reversed_changed_pixels','shadow_time_lag_seconds_max','cloud_time_lag_seconds_max']:
 if temporal.get(field)!=0:errors.append('Stability regression '+field)
out={'revision':'scene-v3','method':'Static HTML links/anchors; all local images decoded; GPU PNGs compared to lossless report WebP; motion all24 frames compared after explicit nearest960x540. Browser layout not rendered: file URL access was rejected by browser policy.','documents':docs,'localImages':images,'losslessReportFigures':identical,'animations':animations,'capturePairs':pairs,'temporalChecks':temporal,'errors':errors}
(MOD/'Evidence/report-check.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'documents':docs,'localImages':len(images),'losslessGPUFigures':len(identical),'animations':animations,'pairs':pairs,'errors':errors},ensure_ascii=False,indent=2))
raise SystemExit(bool(errors))
