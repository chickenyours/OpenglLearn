"""Export the shipped CPU-initialized Lut_Ntt without running PRAGMATA.

Only reads the named PE. Reproduces the native f64 -> f32 -> truncated f16
conversion at VA 0x1485a29d6..0x1485a2a69 (not Python's rounded half pack).
"""
from pathlib import Path
import argparse,collections,hashlib,json,math,struct

EXPECTED='1fc84c99e55f2a87c7e659a224f3e17984280f048d6281d58b396e9431ae9cdc'
def sha(data):return hashlib.sha256(data).hexdigest()
def native_half(value,cap):
    value=min(cap,value)
    b=struct.unpack('<I',struct.pack('<f',value))[0]
    sign=(b>>16)&0x8000; exponent=(b>>23)&255; mantissa=b&0x7fffff
    if exponent<=112:
        if exponent<102:return sign
        return sign|(((mantissa|0x800000)>>(113-exponent))>>13)
    half_exp=exponent-112
    if half_exp==143:
        if mantissa:return sign|0x7c00|(mantissa>>13)|(1 if mantissa<0x2000 else 0)
        return sign|0x7c00
    if exponent>=143:return sign|0x7c00
    return sign|(half_exp<<10)|(mantissa>>13)

def bilinear(rg,u,v):
    # D3D normalized texture coordinates: texel centres (i+.5)/128.
    px=max(0.,min(127.,u*128.-.5));py=max(0.,min(127.,v*128.-.5))
    x0=int(math.floor(px));y0=int(math.floor(py));x1=min(x0+1,127);y1=min(y0+1,127)
    fx=px-x0;fy=py-y0
    return [sum(rg[2*(y*128+x)+c]*w for x,y,w in [(x0,y0,(1-fx)*(1-fy)),(x1,y0,fx*(1-fy)),(x0,y1,(1-fx)*fy),(x1,y1,fx*fy)]) for c in range(2)]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',default='F:/Asset/PRAGMATA/PRAGMATA.exe')
    ap.add_argument('--out',default='E:/REA/pragmata/hair_ntt');ap.add_argument('--asset-dir',default='C:/Users/16620/Desktop/projects/OpenglLearn/Asset/Diana')
    a=ap.parse_args();out=Path(a.out);out.mkdir(parents=True,exist_ok=True);asset=Path(a.asset_dir);asset.mkdir(parents=True,exist_ok=True)
    with Path(a.exe).open('rb') as f:data=f.read()
    assert sha(data)==EXPECTED,'PE differs; revalidate source addresses first'
    pe=struct.unpack_from('<I',data,0x3c)[0];opt=pe+24;base=struct.unpack_from('<Q',data,opt+24)[0]
    sections=[];tab=opt+struct.unpack_from('<H',data,pe+20)[0]
    for i in range(struct.unpack_from('<H',data,pe+6)[0]):
        o=tab+i*40;vs,rva,size,raw=struct.unpack_from('<4I',data,o+8);sections.append((rva,size,raw))
    def raw_at(va):
        rva=va-base;s=next(s for s in sections if s[0]<=rva<s[0]+s[1]);return rva-s[0]+s[2]
    cap_va=0x148fc2d50;cap=struct.unpack_from('<d',data,raw_at(cap_va))[0]
    tables=[];all_values=[]
    for name,va,global_va,creator in [('raster',0x149015a30,0x14ba1d3e0,0x140c21910),('dual_ibl',0x149078880,0x14ba69dc0,0x1485a2900)]:
        raw=raw_at(va);blob=data[raw:raw+0x40000];values=struct.unpack('<32768d',blob)
        assert all(math.isfinite(v) for v in values)
        half=struct.pack('<32768H',*(native_half(v,cap) for v in values))
        rg=struct.unpack('<32768e',half)
        rgba=[component for i in range(16384) for component in (rg[2*i],rg[2*i+1],0.,1.)]
        (out/f'ntt_{name}.f64le').write_bytes(blob);(out/f'ntt_{name}.rg16f').write_bytes(half)
        rgba_bytes=struct.pack('<65536f',*rgba);(out/f'ntt_{name}.rgba32f').write_bytes(rgba_bytes)
        rows=[]
        for y in [0,1,16,32,64,96,126,127]:
            rows.append(dict(y=y,samples=[dict(x=x,rg=list(rg[2*(128*y+x):2*(128*y+x)+2])) for x in [0,1,32,64,96,126,127]]))
        reference_bits=[]
        for value in values:
            # Independent numeric reference, valid for this positive normal table.
            value=struct.unpack('<f',struct.pack('<f',min(cap,value)))[0]
            assert 2**-14<=value<65504
            mant,exponent=math.frexp(value);e=exponent-1
            reference_bits.append(((e+15)<<10)|math.floor((value/2**e-1.)*1024.))
        assert half==struct.pack('<32768H',*reference_bits)
        nearest=struct.unpack('<32768H',b''.join(struct.pack('<e',min(cap,v))for v in values))
        native_bits=struct.unpack('<32768H',half)
        t=dict(name=name,width=128,height=128,channels=2,sourceVA=hex(va),sourceRawOffset=hex(raw),sourceBytes=len(blob),sourceType='f64le',sourceSha256=sha(blob),globalTextureVA=hex(global_va),creatorVA=hex(creator),nativeUploadType='f16le, round toward zero, interleaved RG',nativeUploadSha256=sha(half),nativeUploadBytes=len(half),float32ExpandedSha256=sha(rgba_bytes),float32ExpandedBytes=len(rgba_bytes),rowOrder='native flat memory order, no flip',range=[dict(channel=c,sourceMin=min(values[c::2]),sourceMax=max(values[c::2]),uploadedMin=min(rg[c::2]),uploadedMax=max(rg[c::2])) for c in range(2)],nativeCapValueCount=sum(v>cap for v in values),sourceToUploadMaxError=max(abs(a-b) for a,b in zip(values,rg)),independentMathematicalHalfAuditValues=len(values),differsFromPythonNearestHalfValues=sum(x!=y for x,y in zip(native_bits,nearest)),rowsConstantAcrossX=all(all(rg[2*(128*y+x)+c]==rg[2*(128*y)+c]for x in range(128))for y in range(128)for c in range(2)),rows=rows)
        tables.append(t);all_values.append(rg)
    uv_cases=[(0.,0.),(1.,1.),(.5,.5),(.25,.2),(.7,math.sqrt(.8)),(.5,math.sqrt(.02)),(-.1,1.1)]
    golden=[]
    for u,v in uv_cases:
        samples=[bilinear(rg,u,v) for rg in all_values]
        golden.append(dict(uv=[u,v],raster=samples[0],dual_ibl=samples[1],cosPhiSamples=[dict(cosPhi=c,rasterNtt=samples[0][0]*math.exp(-(samples[0][1]+c)**2),dualIblNtt=samples[1][0]*math.exp(-(samples[1][1]+c)**2)) for c in [-1.,0.,1.]]))
    differences=[abs(a-b) for a,b in zip(*all_values)]
    assert tables[0]['sourceSha256']==tables[1]['sourceSha256']
    payload=struct.pack('<65536f',*(component for i in range(16384)for component in(all_values[1][2*i],all_values[1][2*i+1],0.,1.)))
    lut=struct.pack('<8s4I',b'DHSLUT01',1,128,128,4)+payload
    (asset/'hair-ntt.lutbin').write_bytes(lut)
    asset_info=dict(path='hair-ntt.lutbin',sha256=sha(lut),bytes=len(lut),header='<8s4I: DHSLUT01,1,128,128,4',payload='float32 RGBA; native half R/G exactly expanded, B=0 A=1; no flip or sRGB')
    evidence=dict(schema='pragmata.native-ntt-export.v1',sourceExe=a.exe,sourceExeSha256=EXPECTED,cap=dict(va=hex(cap_va),raw=hex(raw_at(cap_va)),value=cap),tables=tables,asset=asset_info,comparison=dict(identical=all(v==0 for v in differences),differentHalfValues=sum(v!=0 for v in differences),maxAbsDifference=max(differences)),bilinearGolden=golden,consumer=dict(uv=['0.5-0.5*dot(T,L)','sqrt(max(azimuthalRoughness,0.02))'],azimuthalTerm='sample.R * exp(-(sample.G + cosPhi)^2)',sampling='normalized bilinear clamp, LOD 0, linear data',axes='X reverses incident tangent sine; Y is sqrt of azimuthal roughness',identityLimitations='Native embedded data and conversion recovered; original offline table generator remains unknown. The two separate embedded tables were compared byte-for-byte and are identical.'))
    (out/'native_ntt_export.json').write_text(json.dumps(evidence,indent=2)+'\n')
    (asset/'hair-ntt-profile.json').write_text(json.dumps(dict(strandAssets=dict(nttTable='hair-ntt.lutbin'),evidence=dict(schema=evidence['schema'],sourceExeSha256=EXPECTED,rawSourceVA=[t['sourceVA']for t in tables],nativeUploadSha256=tables[1]['nativeUploadSha256'],asset=asset_info,consumer=evidence['consumer'],researchExport=str(out/'native_ntt_export.json'))),indent=2)+'\n')
    public=dict(evidence)
    public['status']='Original shipped lookup data recovered; GPU integration and GPU tests are handled separately.'
    public['nativeCreation']={
        'raster':{'function':['0x140c21910','0x140c22fda'],'descriptor':['0x140c21d78: width=0x80','0x140c21d7d: height=0x80','0x140c21d83: native format argument=0x22'],'sourcePointerInstruction':'0x140c21dc9','capInstruction':'0x140c21dd8','floatConversion':'0x140c21dde','uploadLoopEnd':'0x140c21e47: source byte offset == 0x40000','publish':'0x140c21eae -> global 0x14ba1d3e0'},
        'dualIbl':{'function':['0x1485a2900','0x1485a2b3e'],'descriptor':['0x1485a2976: width=0x80','0x1485a297b: height=0x80','0x1485a2981: native format argument=0x22'],'sourcePointerInstruction':'0x1485a29c7','capInstruction':'0x1485a29d6','floatConversion':'0x1485a29dc','halfTruncation':['0x1485a2a90: exponent << 10','0x1485a2a94: mantissa >> 13, no rounding increment','0x1485a2a61: writes 16-bit value'],'uploadLoopEnd':'0x1485a2a6d: source byte offset == 0x40000','publish':'0x1485a2b01 or 0x1485a2b14 -> global 0x14ba69dc0'},
        'format':'The native creation argument is 34 (0x22), matching DXGI R16G16_FLOAT; the upload independently writes exactly 128*128*2 half values.',
        'bindingProof':[{'load':'0x144aed877 reads global 0x14ba1d3e0','name':'0x144aedf33 loads VA 0x14952dc7d = Lut_Ntt','registrationHash':'0x117ff278','store':'0x144aed88f publishes value under the resolved scene binding index'},{'load':'0x1485a5951 reads global 0x14ba69dc0','name':'0x1485a5d63 loads the same Lut_Ntt string','registrationHash':'0x117ff278','store':'0x1485a5969 publishes value under the resolved scene binding index'}]}
    public['consumerSource']={
        'ibl':{'file':'E:/REA/pragmata/shader/disassembly/gradienttexture/StrandsDualIBLOpt.compute.dxil.txt','sha256':'500e658b9eff81390e21673e333f5700f7715f1b32872b61286c5ef49fecfb65','bindingLines':[542,559],'ssa':['%6542=max(azimuthalRoughness,.02)','%7122=dot(T,L)','%7234..7236 construct UV','%7237..7241 fetch Texture2D<2xF32> through BilinearClamp s5 space32 at LOD0','%7242..7245 compute exp(-(G+cosPhi)^2)','%7251..7252 multiply exp by R; the remaining factors belong to longitudinal TT/Fresnel/absorption'], 'lineRange':[8465,8484]},
        'direct':{'file':'E:/REA/pragmata/shader/disassembly/render/shading.compute.dxil.txt','binding':'Lut_Ntt t12; BilinearClamp s5 space32','ssa':'%1276..1294, repeated %2455..2473 / %3430..3448 / %3801..3819: same RG lookup convention and azimuthal expression'},
        'externalBindingCrossCheck':'E:/REA/pragmata/formats/reasy_sources/resources/data/strands/pragmata.251112995.json maps Lut_Ntt to scene.lut_ntt; native pointer-to-name publishing is the direct shipped-code evidence.'}
    public['scopeAndLimits']=[
        'The source table is the original EXE-embedded data, not the separate computeDSLUT analytic fit.',
        'Both independent embedded byte ranges and their converted half tables are identical, so one asset can serve both consumers.',
        'All rows are constant across X in this shipped table. The source still supplies incident-angle X coordinates; the reconstruction retains that contract.',
        'The original offline algorithm used to produce the embedded doubles is unknown. It is unnecessary for replaying the shipped table.',
        'The source caps each double at 20.0 before f32 conversion. A large positive R sentinel in early rows must not be exported as an uncapped value.',
        'R is the multiplicative azimuthal coefficient, G is the additive shift inside the squared exponent. They are not color, roughness, af/ab, or DSLUT moment channels.',
        'This recovery validates Ntt data and its local sampling formula; it does not establish equivalence of the entire host IBL closure, volume-direction approximation, or scene lighting adapter.'
    ]
    public['reproduce']={'script':'Module/HairSimulation/Tools/export_pragmata_ntt.py','example':'python Module/HairSimulation/Tools/export_pragmata_ntt.py --exe F:/Asset/PRAGMATA/PRAGMATA.exe --out E:/REA/pragmata/hair_ntt --asset-dir Asset/Diana','researchFiles':[str(out/'ntt_pe_xrefs.json'),str(out/'ntt_global_xrefs.json'),str(out/'ntt_manager_140c21910.asm.txt'),str(out/'ntt_creator_1485a2400.asm.txt'),str(out/'native_ntt_export.json')],'execution':'Static rb file access only; no game process, remote attach, or native code execution.'}
    (asset/'hair-ntt-evidence.json').write_text(json.dumps(public,indent=2)+'\n')
    print(json.dumps(dict(asset=asset_info,comparison=evidence['comparison'],independentHalfAudits=[t['independentMathematicalHalfAuditValues']for t in tables],rowConstant=[t['rowsConstantAcrossX']for t in tables]),indent=2))
if __name__=='__main__':main()
