"""Package the static chapter-three candidate, preserving recovered half bits.

This copies source evidence; it does not calibrate color, exposure or light state.
"""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    payload = args.source.read_bytes()
    profile = json.loads(payload)
    if profile['schema'] != 'model.hair-direct-lighting.v1':
        raise ValueError('Unexpected direct-light profile')
    if not 0 < len(profile['lights']) <= 64:
        raise ValueError('Invalid source light count')
    for light in profile['lights']:
        packed = light['sourcePacked']
        for source, target in [('toleranceBits', 'toleranceBits'), ('extXBits', 'extendedBits')]:
            value = int(packed[source], 0)
            if not 0 <= value <= 0xffffffff:
                raise ValueError('Invalid packed light word')
            light[target] = value
    profile['source']['inputProfileSha256'] = hashlib.sha256(payload).hexdigest()
    profile['source']['packaging'] = (
        'Promote original sourcePacked hex words to loader uint32 fields; '
        'preserve native truncated half coefficients without re-quantization. '
        'All placement, color, intensity and study assumptions remain as supplied.')
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    args.destination.write_text(json.dumps(profile, ensure_ascii=False, indent=2,
                                          allow_nan=False)+'\n', encoding='utf8')
    print(json.dumps(dict(lights=len(profile['lights']), output=str(args.destination),
                          sha256=hashlib.sha256(args.destination.read_bytes()).hexdigest())))


if __name__ == '__main__':
    main()
