#!/usr/bin/env python3
"""Fix UploadResult mutex lifetime in Terrain::System::MeshUploadSystem."""
from pathlib import Path
import base64
import subprocess
import sys
import zlib

VERSION = "2026-09-19-terrain-upload-mutex-v3"
PAYLOAD = """c-oa#O;g)25WVMDY%d*qj2*s8nS^F&(++UpLMWFG1IFGE4UXioB!;x`-@9@g;}1CrKE#pq-jlWaByG&5Q}F#qrXcKJidZMuzs4*J
8ShUPQsG?o)v|pjm+fE%4_iZfzVClPx#xM_N7PSG!9P0Q+4}+>y$<%_^t1=7XDTvBs(3Jv>@VIaNWukX@C|l{#orT{qnrhKLk0%r
xI^%~yu*cm?^5Q81eXfNsh;Qu!}|<Ts!6V*@h5-X4(r6T3gt1UWXNO8nk2tUVpE5(K}tx*AI-<qhRu_&EfLYv-J=e(_#|)7wZfMX
nC_~8KgtK(VOyp?lRt_DN9%kxu+&k67-QVYu(4(#j}r_o<V~2c*l}GGmtfgT;jv(c-Qot#D4s84p}A_(-$yn`=rj=_E#wNHnPh@D
g|ec5*z+1o3$+qZ;OZ<B6t6kigAfUrAQN^PCK87om@>9&fLFPb27~4M3Z>9lgy(t8PF90Am{IP3=;;)*Ns7^kygW}6R2ZKZ(e<q6
(u}KvecC!D{-PS_-F3X9!z!;5RU69^rmY;_a<^5c2zJfk9Z*W=%Ku)MTdH|!Z-iktu@Utueb$wS7PpoGtIaoSRI*|5x0U;5-#rzx
2N_CKjx7mqJ+Ix_?~{9p0fHwB2&Z(qgPLKUSeS(p9uPSll?jFt;{fi9dG3Msg0Sj6A*K?3=R29Sb)vwbzyebemJT5<dq$u*v4m!0
5@aM&1d^6PTXT~gXuNg;dTUO=M)o^Tzs-BI^W?LbWCPuD>a|?bA@SB3l8PzdTWK-ZZWfE~x@JTE^|fOe#l38PR-LZ7!WHA1Vj8ch
d#+KcvermhiV5RXKTvNFrD?l7S&z3@KpQyLGH3(G)Iwj6caFiM-@U!E6bhxJ^WRw!f@?+xr-=l<DAy#QfSFs&go`X7BP8G%s_&7a
?J_1u&@SNWr1+G<U_8_O>5|YRhx(aX81%(Y$6amzTy9zCY%jRHuq=|*h2(iXGv@I}MFq8c8$8nxi*QxO83XJeZpEY>)F2)lm8)11
KNSUAi@UOR9o~g*l|A*(Lmmmk`^94?zwb*xnY)P+X_>oXFhiv?Zm!k;=QMJ;"""
TARGET = "Module/Terrain/Systems/terrain_systems.h"
OLD_NEEDLE = "std::lock_guard<std::mutex> lock(gpu.pending->mutex);"
FIXED_NEEDLE = "if(auto pending = gpu.pending)"


def apply(root: Path, args: list[str], data: bytes) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "apply", *args, "-"], cwd=root, input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main() -> int:
    print(VERSION)
    root = Path(__file__).resolve().parent
    target = root / TARGET
    if not target.is_file():
        print("error: V2 terrain pipeline is not installed", file=sys.stderr)
        return 2
    source = target.read_text(encoding="utf-8-sig")
    if FIXED_NEEDLE in source:
        print("hotfix is already applied")
        return 0
    if OLD_NEEDLE not in source:
        print("error: terrain_systems.h is neither the expected V2 nor fixed version; no files changed", file=sys.stderr)
        return 1
    patch = zlib.decompress(base64.b85decode("".join(PAYLOAD.split()).encode("ascii")))
    common = ["--ignore-space-change", "--whitespace=nowarn"]
    check = apply(root, ["--check", *common], patch)
    if check.returncode != 0:
        sys.stderr.buffer.write(check.stderr)
        print("error: hotfix check failed; no files changed", file=sys.stderr)
        return 1
    result = apply(root, common, patch)
    if result.returncode != 0:
        sys.stderr.buffer.write(result.stderr)
        return 1
    print("terrain upload mutex lifetime fixed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
