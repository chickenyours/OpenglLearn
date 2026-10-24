from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit,unquote
from PIL import Image,ImageChops
import json, hashlib
MOD=Path(__file__).resolve().parents[1]
class Doc(HTMLParser):
 def __init__(self):super().__init__();self.refs=[];self.ids=[];self.images=[]
 def handle_starttag(self,tag,attrs):
  a=dict(attrs)
  if 'id' in a:self.ids.append(a['id'])
  for k in ('src','href'):
   if k in a:self.refs.append(a[k])
  if tag=='img':self.images.append(a)
errors=[];docs={};images={};lossless=[]
for name in ['index.html','foundation.html']:
 file=MOD/'Report'/name;d=Doc();d.feed(file.read_text(encoding='utf8'))
 for ref in d.refs:
  u=urlsplit(ref)
  if u.scheme:continue
  if not u.path:
   if u.fragment and u.fragment not in d.ids:errors.append(name+': missing anchor '+ref)
  elif not (file.parent/unquote(u.path)).resolve().exists():errors.append(name+': missing path '+ref)
 if len(d.ids)!=len(set(d.ids)):errors.append(name+': duplicate IDs')
 for a in d.images:
  if not a.get('alt'):errors.append(name+': missing image alt')
  if urlsplit(a['src']).scheme:continue
  p=file.parent/a['src']
  if not p.exists():continue
  with Image.open(p) as im:im.verify()
  with Image.open(p) as im:images[str(p.relative_to(MOD))]={'size':im.size,'frames':getattr(im,'n_frames',1)}
 docs[name]={'references':len(d.refs),'imageOccurrences':len(d.images),'anchors':len(d.ids)}
for src in (MOD/'Captures/lighting-v2').glob('*.png'):
 target=MOD/'Report/images/lighting-v2'/(src.stem+'.webp')
 a=Image.open(src).convert('RGB');b=Image.open(target).convert('RGB')
 same=a.size==b.size and ImageChops.difference(a,b).getbbox() is None
 if not same:errors.append('Report image differs from GPU capture '+src.name)
 lossless.append({'capture':src.name,'reportImage':str(target.relative_to(MOD)),'rgbPixelIdentical':same,'gpuCaptureSha256':hashlib.sha256(src.read_bytes()).hexdigest()})
pairs={}
for kind,a,b in [('sun-shadow','sun-shadow-off','sun-shadow-on'),('PCF','pcf-off','pcf-on'),('AO','contact-ao-off','contact-ao-on'),('cloud','clouds-off','clouds-on'),('cloud-time','cloudmap-only','cloudmap-later'),('lighting','lighting-off','lighting-on'),('post','post-off','post-on')]:
 diff=ImageChops.difference(Image.open(MOD/'Captures/lighting-v2'/(a+'.png')).convert('RGB'),Image.open(MOD/'Captures/lighting-v2'/(b+'.png')).convert('RGB'))
 changed=sum(1 for px in diff.getdata() if max(px)>3)
 pairs[kind]={'above3ChangedPixels':changed,'a':a+'.png','b':b+'.png'}
 if not changed:errors.append('No visible difference: '+kind)
out={'revision':'lighting-v2','method':'Static HTML links/anchors/image decoding; lossless report images compared to actual GPU PNGs; GPU effect-pair differences. Browser layout not rendered because file URL access was rejected by browser policy.','documents':docs,'localImages':images,'capturePairs':pairs,'losslessReportFigures':lossless,'errors':errors}
(MOD/'Evidence/report-check.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'documents':docs,'localImages':len(images),'losslessGPUFigures':len(lossless),'pairs':pairs,'errors':errors},ensure_ascii=False,indent=2))
raise SystemExit(bool(errors))
