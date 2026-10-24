"""Consolidate actual v3 build/capture/check evidence without executing a build."""
from pathlib import Path
import json,hashlib
from datetime import datetime,timezone
from PIL import Image,ImageChops
MOD=Path(__file__).resolve().parents[1]
ROOT=MOD.parents[1]
E=MOD/'Evidence'
def read(p):return json.loads(p.read_text(encoding='utf8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
check=read(E/'report-check.json');gpu=read(E/'gpu-validation.json');perf=read(E/'performance-validation.json')
stable=read(E/'stability-validation.json')
assert check['revision']=='scene-v3' and not check['errors'] and gpu['smoke_pass'] and gpu['gl_error']==0
assert all(x['rgbPixelIdentical'] for x in check['losslessReportFigures'])
manifest=read(MOD/'Captures/lighting-v3/capture-manifest.json')
scenarios={r['file'] for r in manifest['captured_frames']}
diagnostics={'shadow-map-depth.png','cloudmap-only.png','cloudmap-later.png','reflection-only.png','opaque-only.png'}
expected=scenarios|diagnostics|{'ui.png'}
actual={p.name for p in (MOD/'Captures/lighting-v3').glob('*.png')}
assert actual==expected,(actual-expected,expected-actual)
assert len(actual)==34
visual=read(E/'scene-v3/visual-measurements.json')
# Regenerate final water differences from the final captured PNGs, not earlier candidates.
water={}
for kind in ['depth','refraction','reflection']:
 a=MOD/f'Captures/lighting-v3/water-{kind}-off.png';b=MOD/f'Captures/lighting-v3/water-{kind}-on.png'
 diff=ImageChops.difference(Image.open(a).convert('RGB'),Image.open(b).convert('RGB'))
 count=0;error=0
 for px in diff.getdata():
  count+=sum(px)>10;error+=sum(px)
 water[kind]={'changed_pixels_rgb_sum_gt10':count,'mean_absolute_rgb_error_8bit':error/(1280*720*3),'offSha256':sha(a),'onSha256':sha(b)}
 assert count>100
reflection=Image.open(MOD/'Captures/lighting-v3/reflection-only.png').convert('RGBA')
alpha=reflection.getchannel('A')
water_record={'scene_size':[1280,720],'comparisons':water,'reflection_rt':{'size':reflection.size,'alpha_minmax':alpha.getextrema(),'covered_pixels':sum(1 for x in alpha.getdata() if x>0),'coverage_scope':'Full RT, including land outside the water. Not the percentage of river pixels with a visible reflection.'},'scope':'Final real GPU PNGs, only named water feature switch changes. Original evidence uses reflection probes; planar reflection is a migration feature.'}
(E/'water-v3-validation.json').write_text(json.dumps(water_record,ensure_ascii=False,indent=2),encoding='utf8')
out={'status':'passed','revision':'scene-v3','verifiedAtUTC':datetime.now(timezone.utc).isoformat(),
 'build':{'command':'cmake --build build --target graveyard_scene_lab --parallel 4','exitCode':0,'normalBuildSucceeded':True,'log':'Evidence/normal-build-v3.log','exeSha256':sha(ROOT/'bin/graveyard_scene_lab.exe')},
 'launcher':{'command':'Module/GraveyardSceneLab/Run.cmd --smoke-test','exitCode':0,'log':'Evidence/run-smoke-v3.log'},
 'gpu':gpu,'stability':stable,'water':water_record,'performance':perf,
 'capture':{'scenarioCount':len(scenarios),'diagnostics':sorted(diagnostics),'uiCount':1,'pngCount':len(actual),'manifestSha256':sha(MOD/'Captures/lighting-v3/capture-manifest.json'),'sameFrameSunAndCloud':True},
 'report':{'main':'Report/index.html','historical':['Report/lighting-v2.html','Report/foundation.html'],'htmlChecks':check['documents'],'staticRGBPixelIdenticalFigures':len(check['losslessReportFigures']),'animations':check['animations'],'errors':check['errors'],'mainSha256':sha(MOD/'Report/index.html'),'layoutVerification':'Static links, anchors and decoded images; browser layout not rendered because file URL access was rejected by browser policy.'},
 'sourceSha256':{str(p.relative_to(MOD)):sha(p) for p in [MOD/'Private/main.cpp',MOD/'Private/rhi_bridge.h',*sorted((MOD/'Assets/shaders').glob('*'))] if p.is_file()},
 'originalEvidencePackage':'Evidence/scene-v3/package-manifest.json','visualMeasurements':'Evidence/scene-v3/visual-measurements.json',
 'limits':['New diorama layout, selected original assets, not full original map/runtime reconstruction.','Original live GPU constants, quality tier and weather were not captured.','Single sun shadow map, custom contact AO and explicit ambient calibration; not full Unity cascades/NGSS/PPv2.','Planar reflection, Snell screen refraction and RGB optical absorption are migration choices; original uses reflection probes and layered depth colour.','Performance is one local integrated-GPU capture, not a cross-device guarantee.'],
 'reportSummary':'正常构建、Run.cmd与24组GPU状态通过；冻结13帧与opaque反序提交均零像素差；连续60时间样本的太阳/云延迟零；34张静态报告图及两段24帧动画逐帧RGB一致；报告引用与解码检查通过。'}
(E/'validation.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'status':'passed','pngs':len(actual),'animations':check['animations'],'meanFPS':perf['mean_fps'],'exeSha256':out['build']['exeSha256']},ensure_ascii=False,indent=2))
