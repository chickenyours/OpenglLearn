"""Inventory and disassemble selected original display-chain DXIL, offline."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

SELECTION = {
    'tonemap': {'PostTonemap', 'PostTonemapC4L', 'PostTonemap3Section',
        'PostTonemapC4L3Section', 'ApplyTonemapAndDisplayOutput',
        'ApplyExposure', 'ApplyExposureACEScc', 'ApplyExposureForBeforeGrading',
        'WhitePoint', 'WhitePoint_Legacy', 'WriteGlobalWhitePoint',
        'DebugTonemapCurve'},
    'screenoutput': {'ScreenOutputSRGB', 'ScreenOutputRec709',
        'ScreenOutputGradingSpace', 'ScreenOutputSRGBDisable'},
    'advancedsystem': {'GenHDRLUTLinear', 'GenHDRLUTLinear32',
        'ColorCorrectWithUIMask', 'ConvertColorSpaceSDR',
        'LDRPostProcessWithTonemap3Section', 'LDRPostProcessWithExposure',
        'LDRPostProcess', 'CopyWithOCIOLUT',
        'CaptureToTextureLUTTransformWithTripleSection'},
}

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--research', type=Path, default=Path('E:/REA/pragmata'))
    ap.add_argument('--output', type=Path, default=Path('E:/REA/pragmata/hair_display'))
    ap.add_argument('--dxc', type=Path, default=Path('C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/dxc.exe'))
    args = ap.parse_args()
    spec = importlib.util.spec_from_file_location('existing_sdf_parser', args.research/'shader-tools/inspect_pragmata_sdf.py')
    sdf = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdf)
    dest = args.output/'disassembly'
    dest.mkdir(parents=True, exist_ok=True)
    records = []
    for name, selected in SELECTION.items():
        path = args.output/'extracted/natives/stm/systems/shader'/(name+'.sdf.251112995')
        if name == 'advancedsystem':
            path = args.research/'hair_lighting/extracted/natives/stm/systems/shader/advancedsystem.sdf.251112995'
        data = path.read_bytes()
        inv = sdf.parse(path, {'containers': []})
        (args.output/(name+'_inventory.json')).write_text(json.dumps(inv, indent=2)+'\n', encoding='utf-8')
        for p in inv['programs']:
            if p['target'] != 2 or p['name'] not in selected:
                continue
            for shader in p['shaders']:
                if shader['stage'] == 'vertex':
                    continue
                raw = data[shader['offset']:shader['offset']+shader['size']]
                assert raw[:4] == b'DXBC'
                stem = name+'.'+p['name']+'.'+shader['stage']
                binary = dest/(stem+'.dxbc')
                binary.write_bytes(raw)
                text = dest/(stem+'.txt')
                result = subprocess.run([str(args.dxc), '-dumpbin', '-Fc', str(text), str(binary)], capture_output=True, text=True)
                assert result.returncode == 0, result.stderr
                records.append(dict(source=str(path), sourceSha256=hashlib.sha256(data).hexdigest(),
                    program=p['name'], programRecordOffset=p['record_offset'], target=2,
                    stage=shader['stage'], offset=shader['offset'], bytes=len(raw),
                    sha256=hashlib.sha256(raw).hexdigest(), binary=str(binary), text=str(text),
                    textSha256=hashlib.sha256(text.read_bytes()).hexdigest()))
    (args.output/'shader_manifest.json').write_text(json.dumps(dict(records=records), indent=2)+'\n', encoding='utf-8')
    for r in records:
        print(r['program'], r['stage'], r['bytes'], r['sha256'])

if __name__ == '__main__':
    main()
