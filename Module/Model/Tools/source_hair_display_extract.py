"""Extract a bounded original display-chain research set; never run PRAGMATA.

Uses the existing reviewed PAK reader/TOCs. Archives are opened rb only. Output
is confined to --output (default E:/REA/pragmata/hair_display). No installation,
graphics device, source exposure conversion, or shared project build is used.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

MEMBERS = [
    'natives/stm/systems/shader/tonemap.sdf.251112995',
    'natives/stm/systems/shader/screenoutput.sdf.251112995',
    'natives/stm/systems/shader/sceneoutputmm.sdf.251112995',
    'natives/stm/light/colorcube/hdr_linear.tex.251111100',
    'natives/stm/light/colorcube/st/st30/cc_st30_100_00.tex.251111100',
    'natives/stm/light/colorcube/st/st30/cc_st30_200_00.tex.251111100',
    'natives/stm/systems/rendering/colorcubelinear.tex.251111100',
]

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--research', type=Path, default=Path('E:/REA/pragmata'))
    ap.add_argument('--output', type=Path, default=Path('E:/REA/pragmata/hair_display'))
    args = ap.parse_args()
    sys.path.insert(0, str(args.research/'tools'))
    from pak_io import bounded_decode, safe_destination
    args.output.mkdir(parents=True, exist_ok=True)
    wanted = set(MEMBERS)
    lookup, indices = {}, []
    for name in ['re_chunk_000.pak.index.json', 're_chunk_000.pak.sub_000.pak.index.json']:
        p = args.research/'evidence/pak'/name
        raw = p.read_bytes()
        idx = json.loads(raw)
        indices.append(dict(path=str(p), sha256=hashlib.sha256(raw).hexdigest(),
            archive=idx['path'], storedTocSha256=idx['stored_toc_sha256'],
            decodedTocSha256=idx['decoded_toc_sha256']))
        for e in idx['entries']:
            if e.get('path') in wanted:
                lookup[e['path']] = dict(e, archive=idx['path'])
    records, total = [], 0
    for member in MEMBERS:
        e = lookup[member]
        stored = e['compressed_size'] or e['decompressed_size']
        assert e['bounds_valid'] and stored <= 128*1024*1024
        assert e['decompressed_size'] <= 128*1024*1024
        total += e['decompressed_size']
        assert total <= 256*1024*1024
        with Path(e['archive']).open('rb') as f:
            f.seek(e['offset'])
            packed = f.read(stored)
        assert len(packed) == stored
        data = bounded_decode(packed, e, 128*1024*1024)
        assert len(data) <= 128*1024*1024
        dest = safe_destination(args.output/'extracted', member)
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            assert dest.read_bytes() == data, 'Existing extracted source differs'
        else:
            dest.write_bytes(data)
        records.append(dict(member=member, archive=e['archive'], offset=e['offset'],
            storedBytes=stored, decodedBytes=len(data),
            sha256=hashlib.sha256(data).hexdigest(), output=str(dest),
            compression=e['compression'], remapped=e['remapped']))
    result = dict(schema='pragmata.source-hair-display-extraction.v1',
        indexRecords=indices, records=records, targetExecuted=False)
    (args.output/'extraction.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    for r in records:
        print(r['member'], r['decodedBytes'], r['sha256'])

if __name__ == '__main__':
    main()
