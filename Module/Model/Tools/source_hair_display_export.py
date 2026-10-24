"""Export original 3D ColorCube values with exact half-to-float expansion.

Source archives must first be read by source_hair_display_extract.py. The
existing Microsoft DirectStorage CPU codec is reused for GDeflate. No GPU,
native game, installation or shared CMake build is invoked.
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import numpy as np
from source_hair_display_reference import parameters, cct_encode, cct_decode, curve

def sha(data):return hashlib.sha256(data).hexdigest()

def sample_cube(a,rgb):
    n=a.shape[0];q=np.clip(np.asarray(rgb),0,1)*(n-1)
    lo=np.floor(q).astype(int);hi=np.minimum(lo+1,n-1);f=q-lo
    result=np.zeros(3)
    for z in range(2):
        for y in range(2):
            for x in range(2):
                i=[hi[c]if t else lo[c]for c,t in enumerate([x,y,z])]
                w=np.prod([f[c]if t else 1-f[c]for c,t in enumerate([x,y,z])])
                result+=a[i[2],i[1],i[0],:3]*w
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--research',type=Path,default=Path('E:/REA/pragmata'))
    ap.add_argument('--output',type=Path,default=Path('E:/REA/pragmata/hair_display'))
    ap.add_argument('--asset-dir',type=Path,default=Path('C:/Users/16620/Desktop/projects/OpenglLearn/Asset/Diana'))
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True);args.asset_dir.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('existing_gdeflate_codec',args.research/'diana_full/extract_diana_materials.py')
    codec=importlib.util.module_from_spec(spec);spec.loader.exec_module(codec);decoder=codec.GDeflateCPU()
    records=[];decoded=args.output/'decoded';decoded.mkdir(exist_ok=True)
    for stem,name in [('hdr_linear','hdr-linear-display'),('cc_st30_100_00','cc-st30-100-display'),('cc_st30_200_00','cc-st30-200-display')]:
        paths=list((args.output/'extracted').rglob(stem+'.tex.251111100'));assert len(paths)==1
        path=paths[0];b=path.read_bytes();magic,ver,w,h,d,images,mb,fmt=struct.unpack_from('<IIHHHBBI',b)
        assert magic==0x00584554 and ver==251111100 and w==h==d==33 and images==1 and mb==16 and fmt==10
        _,pitch,slice_pitch=struct.unpack_from('<QII',b,40);size,off=struct.unpack_from('<II',b,56)
        src=b[64+off:64+off+size];assert len(src)==size
        raw=decoder.decompress(src,slice_pitch*d)if src[:2]==b'\x04\xfb'else src
        assert len(raw)==slice_pitch*d and pitch>=w*8 and slice_pitch>=pitch*h
        tight=b''.join(raw[z*slice_pitch+y*pitch:z*slice_pitch+y*pitch+w*8]for z in range(d)for y in range(h))
        a=np.frombuffer(tight,dtype='<f2').astype('<f4').reshape(d,h,w,4)
        assert np.isfinite(a).all() and (a>=0).all() and (a<=1.01).all()
        assert (a[...,3]==1).all()
        (decoded/(stem+'.rgba16f')).write_bytes(tight)
        payload=a.tobytes();lut=struct.pack('<8s4I',b'DHSLUT01',1,w,h*d,4)+payload
        asset=args.asset_dir/(name+'.lutbin')
        if asset.exists():assert asset.read_bytes()==lut, 'Existing source display table differs'
        else:asset.write_bytes(lut)
        gold=[]
        for linear in [[0,0,0],[.018,.018,.018],[.18,.18,.18],[1,1,1],[.05,.1,.3],[2,.4,.1]]:
            enc=[cct_encode(x)for x in linear];sample=sample_cube(a,enc)
            gold.append(dict(linear=linear,encoded=enc,sampled=sample.tolist(),decoded=[max(0,cct_decode(x))for x in sample]))
        r=dict(source=str(path),sourceSha256=sha(b),dxgiFormat=10,sourceDimensions=[w,h,d],
            pitch=pitch,slicePitch=slice_pitch,sourceTightHalfSha256=sha(tight),
            payloadSha256=sha(payload),asset=asset.name,assetSha256=sha(lut),assetBytes=len(lut),
            transportHeader=['DHSLUT01',1,w,h*d,4],sourceOrder='z/y/x/RGBA; x=red,y=green,z=blue',
            encoding='Original ACEScct-encoded data values; float16 expanded exactly to float32. No sRGB decode, exposure, rotation or source output adjustment.',
            golden=gold)
        if stem=='hdr_linear':
            z,y,x=np.meshgrid(np.arange(d)/32,np.arange(h)/32,np.arange(w)/32,indexing='ij')
            r['axisIdentityMaximumError']=float(np.max(np.abs(a[...,:3]-np.stack([x,y,z],axis=-1))))
            assert r['axisIdentityMaximumError']<.00002
        records.append(r)
    extraction=json.loads((args.output/'extraction.json').read_text())
    shaders=json.loads((args.output/'shader_manifest.json').read_text())
    reference=json.loads((args.output/'reference_validation.json').read_text())
    typed=json.loads((args.research/'hair_lighting/typed_lighting.json').read_text())
    selected=[]
    for record in typed['records']:
        if not any(n in record['path']for n in ['st30_broad_light_edit.scn','st30_broad_post.scn','st30_101_post.scn']):continue
        instances=[]
        for x in record.get('instances',[]):
            if x['type']=='via.render.ToneMapping' or x['type']=='app.posteffect.ToneMappingParam' or x['type']=='app.posteffect.ColorCorrectParam':
                fields={k:v for k,v in x['fields'].items()if k in ['_Enabled','_ColorCube','EV','_EV','AutoExposure','_AutoExposure','ExposureEnable','TonemapEnable','TonemapRange','Contrast','LinearSectionBegin','LinearSectionLength','SDRToe','HDRToe','HuePreserveEnable','HighlightDeSaturation']}
                instances.append(dict(instance=x['instance'],type=x['type'],fields=fields))
        selected.append(dict(path=record['path'],sha256=record['sha256'],bytes=record['bytes'],instances=instances))
    evidence=dict(schema='pragmata.source-hair-display.v1',status='Original 3D resources and selected SDR formulas recovered; runtime display chain remains unverified',
        extraction=extraction,shaders=shaders,tables=records,selectedSceneRecords=selected,
        cpuFormulaAudit=reference,
        threeSection=dict(shader='tonemap.PostTonemap3Section.pixel.txt',sourceSSA=['%139..238'],
            coefficientMeaning='Three exact vec4 source CB groups at bytes0..47. Scalar curve mixes toe, linear and exponential shoulder; DXIL Exp is exp2.',
            coefficientProducer='Native writer 0x140391370..0x140391b96, arithmetic0x140391448..0x140391517, writes three CB vec4 groups. MakeSectionStudy follows this recovered construction; caller state remains unknown.',
            sourceStatic=[1.,.2199999988079071,.4000000059604645,1.],
            studyMaxNit=1.,studyMaxNitMeaning='Native CPU conditional ordinary-output branch initializesP=1; another state/HDR branch uses an unidentified display-manager scalar times.01. Current study explicitly choosesP1; NOT a conversion of TonemapRange.1 or scene EV.'),
        colorCube=dict(shader='advancedsystem.ColorCorrectWithUIMask.pixel.txt',encodeSSA='%45..80',lookupSSA='%81..86',decodeSSA='%87..131',
            coordinate='ACEScct(sourceLinearRGB)*(1-1/N)+.5/N; TrilinearClamp LOD0. Exact CPU/flat sampling uses q=clamp(ACEScct,0,1)*(N-1).',
            inputOutput='ACEScct encode -> sample authored encoded RGB -> ACEScct decode -> max0; no OCIO gamut conversion is silently inserted.'),
        output=dict(shader='screenoutput.ScreenOutputSRGB.pixel.txt',sourceSSA='%13..64',
            formula='sRGB OETF, then pow(encoded,fGamma)*fConvertToLimit+fLowerLimit. This differs from blindly applying pow(linear,1/2.2).',
            runtimeConstants='fGamma/fConvertToLimit/fLowerLimit were not captured. Study identity uses1/1/0 explicitly as host choices.'),
        limitations=['ST30_101 has not been tied to the reference image/chapter; broad-scene layouter/volume activation and interpolation remain unknown.',
            'Source static IBL Exposure0/300/600/900 is not decoded as EV. Source whitepoint, output CB and OCIO matrices/RGC defaults are not captured.',
            'PostTonemap versus 3Section/ACEScc/ACEScct/HDR/C4L selection is not runtime-proven. A standalone optional study is not whole-game display equivalence.',
            'The HDR_Linear authored table is nearly identity in encoded coordinates but has quantization; do not substitute an unproven color-space matrix.',
            'Source LUTs, exact half expansion and local formula audits do not establish original GPU bit identity or screenshot similarity.'])
    tool_paths=[args.research/'shader-tools/inspect_pragmata_sdf.py',
        Path('C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe'),
        args.research/'diana_full/codec/official/native/bin/x64/dstorage.dll',
        args.research/'diana_full/codec/official/native/bin/x64/dstoragecore.dll']
    evidence['offlineToolIdentity']=[dict(path=str(p),sha256=sha(p.read_bytes()))for p in tool_paths]
    evidence['reproduction']=[
        'python Module/Model/Tools/source_hair_display_extract.py',
        'python Module/Model/Tools/source_hair_display_inspect.py',
        'python Module/Model/Tools/source_hair_display_native.py --dump-candidates',
        'python Module/Model/Tools/source_hair_display_reference.py',
        'python Module/Model/Tools/source_hair_display_export.py',
    ]
    evidence['cpuValidationScope']='Only standalone offline Python arithmetic/data validation is performed by these tools. Production Render GPU/Model tests are separate and must not be inferred from the local reference audit.'
    source_asm=args.output/'native_140391370.asm.txt'
    if source_asm.exists():
        focus=json.loads((args.output/'native_focus.json').read_text())
        relevant=['native_140391370.asm.txt','native_1402101d0.asm.txt','native_144fb2200.asm.txt','native_148442e70.asm.txt','native_144af1510.asm.txt','native_140210f30.asm.txt']
        evidence['nativeCpuEvidence']=dict(sourceExe='F:/Asset/PRAGMATA/PRAGMATA.exe',
            sourceExeSha256='1fc84c99e55f2a87c7e659a224f3e17984280f048d6281d58b396e9431ae9cdc',
            interpretation='Static shipped instructions, not observed execution or final frame values.',
            files=[dict(path=str(args.output/n),sha256=sha((args.output/n).read_bytes()))for n in relevant],
            constants=focus['constants'],
            writer=dict(function=['0x140391370','0x140391b96'],
                caller='0x144fb242e loads owner+178 as target CB;0x144fb2444 calls writer with same owner and target',
                binding='0x140210216 loads owner+178;0x140210265 resolves TonemapParam;0x14021022a publishes target under resolved slot',
                coefficients='0x140391448..0x140391517 populates CB0..40. Inputs from owner+5b8/+5bc/+5c0 and selected toe+5c4/+5c8.',
                formula=['l0=(P-m)*length/a','S0=m+l0','S1=m+a*l0','remaining=P-S1','C2=a*P/remaining',
                    'factor=(1/P)*C2*(-1.4426950216293335)','offset=-S0*factor','invM=1/m','linearOffset=m-a*m'],
                peak='0x140391414 constant1;0x140391420 copiesP1;conditional0x140391429..438 loads global-manager+c8 times.009999999776482582. Unknown manager-state predicate and actual scalar.',
                upload='0x14039189a selects stack+a8;0x1403918a9 sets bytecount220;0x1403918b2 copies to mapped target',
                tonemapRange='Not read in this recovered coefficient construction. No TonemapRange.1->P or sceneEV mapping is asserted.'),
            lutCoordinates=dict(function=['0x140210f30','0x140211117'],
                size='0x14021101e..1030 loads actual primary texture width and converts to float',
                inverse='0x14021104a computes1/N;0x14021104e multiplies.5;0x140211060 computes1-1/N',
                upload='0x1402110a3..10bf copies96B ColorCorrectTexture block; binding name at0x1402110e9'))
        manager=args.output/'native_140c21910.asm.txt'
        if not manager.exists():manager=args.research/'hair_ntt/ntt_manager_140c21910.asm.txt'
        evidence['nativeDslutDescriptor']=dict(dimensions=[64,64,16],dxgiFormat=10,formatName='R16G16B16A16_FLOAT',
            creation=dict(file=str(manager),sha256=sha(manager.read_bytes()),
                format='0x140c22336 stack format argument0x0a',
                dimensions=['0x140c22341 EDX64','0x140c22346 R8D64','0x140c2234c R9D16'],
                descriptorCall='0x140c22352 ->0x140145540',resourceCreation='0x140c22360 ->0x140145350',
                uavPublication='0x140c223b0 ->owner+848',srvPublication='0x140c223ff ->owner+850'),
            cpuCounts='0x144af1637..1648 writes64,64,16 toDSInfo+16/+20/+24;0x144af1a78..1a88 copies160B.',
            producerBinding='0x144af235a loadsowner+848;0x144af2650 resolvesRWDSLUT;0x144af2372 publishesit.',
            consumerBinding='0x144af239e loadsowner+850;0x144af2622 resolvesDSLUT;0x144af23b6 publishesit.',
            coordinate='Producer texel centres(x+.5)/64,(y+.5)/64,(z+.5)/16. Consumer roughness clamp(hm_v,.18,.6) with3D filtering; theta=abs(si),absorption=saturate(.0138565180823*log2(max(base,.001))^2).',
            limits=['This closes the inspected native raster manager route; no runtime resource/view capture proves the active frame variant.',
                'Separate dual-IBL owner+3d8 route dispatches64/64/16 at0x1485a91b7..91d4, but its separate physical creation was not followed.',
                'Descriptor recovery does not itself generate or validate all64*64*16 moment texels.'])
    for dest in [args.output/'display_evidence.json',args.asset_dir/'hair-display-evidence.json']:
        dest.write_text(json.dumps(evidence,indent=2)+'\n',encoding='utf-8')
    profile=dict(schema='model.source-display-study.v1',enabled=False,label='ST30 source three-section study (runtime selection unverified)',
        source='hair-display-evidence.json',table='cc-st30-100-display.lutbin',tableSize=33,tableEncoding='ACEScct',
        alternatives=[r['asset']for r in records],
        curve=dict(contrast=1.,linearBegin=.2199999988079071,linearLength=.4000000059604645,toe=1.,maxNit=1.,
            coefficients=parameters(1.,.2199999988079071,.4000000059604645,1.,1.),
            maxNitIsHostStudyChoice=True,maxNitNativeBranch='P1 exists in source CPU ordinary-output branch; current runtime branch is not captured'),
        adapters=dict(preExposure=1.,ocioMatrix='identity study choice; original not captured',referenceGamutCompression=False,
            displayGamma=1.,convertToLimit=1.,lowerLimit=0.),
        applyOrder=['explicit host preExposure','source SDR three-section core','ACEScct encode / original cube / decode','source sRGB OETF / explicit output adjustment'],
        sourceScene='ST30 broad candidates, not captured reference image lighting',
        note='Opt-in research profile. Does not replace the default ACES preview or claim original EV/white balance.')
    (args.asset_dir/'st30-display-profile.json').write_text(json.dumps(profile,indent=2)+'\n',encoding='utf-8')
    print(json.dumps([dict(asset=r['asset'],sha256=r['assetSha256'],bytes=r['assetBytes'])for r in records],indent=2))

if __name__=='__main__':main()
