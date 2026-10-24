"""Bounded static PE name/reference probe for source display/LUT CPU writers.

LEA candidates are byte-pattern observations, independently confirmed later by
bounded disassembly. The executable is read only and never started.
"""
from __future__ import annotations
import argparse
import bisect
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct
import subprocess

EXPECTED = '1fc84c99e55f2a87c7e659a224f3e17984280f048d6281d58b396e9431ae9cdc'
NAMES = ['TonemapParam', 'linearBegin', 'linearStart', 'LinearSectionBegin',
    'mulLinearStartContrastFactor', 'contrastFactor', 'TonemapRange',
    'PostTonemap3Section', 'ColorCorrectTexture', 'DSLUT', 'RWDSLUT', 'computeDSLUT']

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', type=Path, default=Path('F:/Asset/PRAGMATA/PRAGMATA.exe'))
    ap.add_argument('--output', type=Path, default=Path('E:/REA/pragmata/hair_display'))
    ap.add_argument('--dump-candidates', action='store_true')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with args.exe.open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as b:
        sha = hashlib.sha256(b).hexdigest()
        assert sha == EXPECTED
        pe = struct.unpack_from('<I', b, 60)[0]
        opt = pe+24
        base = struct.unpack_from('<Q', b, opt+24)[0]
        tab = opt+struct.unpack_from('<H', b, pe+20)[0]
        sections = [dict(zip(['virtualSize','rva','rawSize','raw'],struct.unpack_from('<4I', b, tab+40*i+8)))
            for i in range(struct.unpack_from('<H', b, pe+6)[0])]
        def va(raw):
            s = next(s for s in sections if s['raw'] <= raw < s['raw']+s['rawSize'])
            return base+s['rva']+raw-s['raw']
        def raw(addr):
            rva=addr-base
            s=next(s for s in sections if s['rva']<=rva<s['rva']+s['rawSize'])
            return s['raw']+rva-s['rva']
        er, es = struct.unpack_from('<II', b, opt+112+24)
        p = raw(base+er)
        funcs = sorted(struct.unpack_from('<III', b, p+i) for i in range(0,es,12))
        starts = [x[0] for x in funcs]
        def function(addr):
            i=bisect.bisect_right(starts,addr-base)-1
            t=funcs[i]
            return dict(start=hex(base+t[0]),end=hex(base+t[1]),bytes=t[1]-t[0]) if t[0]<=addr-base<t[1] else None
        hits=[]
        for name in NAMES:
            for encoding in ['ascii','utf-16le']:
                needle=name.encode(encoding)+b'\0'*(1 if encoding=='ascii' else 2)
                at=0
                while True:
                    at=b.find(needle,at)
                    if at<0:break
                    hits.append(dict(name=name,encoding=encoding,raw=at,va=hex(va(at)),references=[]))
                    at+=len(needle)
        byva={int(h['va'],16):h for h in hits}
        # RIP-relative LEA r64,[rip+disp32], deliberately only exact target starts.
        pattern=re.compile(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....',re.S)
        for s in sections:
            if s['rva']>=0x9000000:continue
            for m in pattern.finditer(b,s['raw'],s['raw']+s['rawSize']):
                site=va(m.start()); target=site+7+struct.unpack_from('<i',b,m.start()+3)[0]
                if target in byva:
                    byva[target]['references'].append(dict(site=hex(site),bytes=m.group().hex(),function=function(site)))
        result=dict(schema='pragmata.source-hair-display-native-probe.v1',source=str(args.exe),sha256=sha,
            preferredImageBase=hex(base),hits=hits,
            limits=['Static byte-pattern candidate references; each semantic claim needs bounded disassembly.',
                'No runtime target, active frame, exposure, or shader variant was captured.'])
        (args.output/'native_name_candidates.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        constants=[]
        for addr in [0x148fb808c,0x148fb80c0,0x14906e298,0x148fb8098]:
            at=raw(addr);blob=b[at:at+4]
            constants.append(dict(va=hex(addr),raw=hex(at),hex=blob.hex(),float32=struct.unpack('<f',blob)[0]))
        target=0x140391370
        callers=[]
        for section in sections:
            if section['rva']>=0x9000000:continue
            at=section['raw'];end=at+section['rawSize']
            while True:
                at=b.find(b'\xe8',at,end)
                if at<0:break
                if at+5<=end and va(at)+5+struct.unpack_from('<i',b,at+1)[0]==target:
                    callers.append(dict(site=hex(va(at)),function=function(va(at))))
                at+=1
        stores=[]
        lo=raw(0x1485a0000);hi=raw(0x1485b0000)
        for hit in re.finditer(rb'\x48\x89[\x80-\xbf]\xd8\x03\x00\x00',b[lo:hi]):
            addr=va(lo+hit.start());stores.append(dict(va=hex(addr),bytes=hit.group().hex(),function=function(addr)))
        (args.output/'native_focus.json').write_text(json.dumps(dict(constants=constants,
            tonemapWriterCallerCandidates=callers,dualIblDslutFieldStoreCandidates=stores),indent=2)+'\n',encoding='utf-8')
    for h in hits:
        print(h['name'],h['encoding'],h['va'],[(r['site'],r['function'])for r in h['references']])
    if args.dump_candidates:
        llvm = Path('C:/Mydata/clang+llvm-20.1.8-x86_64-pc-windows-msvc/clang+llvm-20.1.8-x86_64-pc-windows-msvc/bin/llvm-objdump.exe')
        selected = {r['function']['start']: r['function'] for h in hits
            if h['name'] in ['TonemapParam','ColorCorrectTexture','DSLUT','RWDSLUT']
            for r in h['references'] if r['function'] and r['function']['bytes']<10000}
        # Followed exact-name binding -> leaf forwarding thunk -> writer and
        # two direct caller sites. Boundaries checked against PE exception data.
        for start,end in [(0x140391370,0x140391b96),(0x148442e70,0x148442ec1),
                (0x144fb2200,0x144fb343e),(0x1485a9920,0x1485a9a81),
                (0x140c21910,0x140c22fda)]:
            selected[hex(start)] = dict(start=hex(start),end=hex(end),bytes=end-start)
        for start, func in selected.items():
            text = subprocess.check_output([str(llvm), '--disassemble',
                '--start-address='+start, '--stop-address='+func['end'], str(args.exe)], text=True)
            p = args.output/('native_'+start[2:]+'.asm.txt')
            p.write_text(text, encoding='utf-8')
            print('dump', p.name, func['bytes'])

if __name__ == '__main__':
    main()
