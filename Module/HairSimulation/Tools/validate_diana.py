"""Check portable Diana assets without the original game/research directory.

Usage: python -B validate_diana.py [Asset/HairSimulation]
Requires numpy. Exits nonzero on any mismatch.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

import numpy as np


def validate(folder):
    metadata = json.loads((folder / "diana_metadata.json").read_text(encoding="utf8"))
    data = (folder / "diana_lod0.hairbin").read_bytes()
    assert hashlib.sha256(data).hexdigest() == metadata["outputSha256"], "Hair checksum"
    magic, version, ng, nn, ns, nv, ne, *bounds = struct.unpack_from("<8s6I6f", data)
    assert magic == b"DHAIR001" and version == 1
    assert (ng, nn, ns, nv, ne) == (120, 3000, 17500, 436975, 419475)
    assert len(data) == 56 + 4*(ng+1) + 32*nn + 4*(ns+1) + 64*nv + 4*ne
    offset = 56
    def read(dtype, count):
        nonlocal offset
        result = np.frombuffer(data, dtype=dtype, count=count, offset=offset)
        offset += count*np.dtype(dtype).itemsize
        return result
    guide_offsets = read("<u4", ng+1)
    guides = read("<f4", nn*4).reshape(-1, 4)
    quats = read("<f4", nn*4).reshape(-1, 4)
    offsets = read("<u4", ns+1)
    render = read("<f4", nv*4).reshape(-1, 4)
    bindings = read(np.dtype([("nodes", "<u4", 4), ("weights", "<f4", 4), ("lerps", "<f4", 4)]), nv)
    segments = read("<u4", ne)
    assert offset == len(data)
    assert np.array_equal(guide_offsets, np.arange(0, nn+1, 25))
    assert offsets[0] == 0 and offsets[-1] == nv and np.all(np.diff(offsets) >= 2)
    assert np.isfinite(guides).all() and np.isfinite(quats).all() and np.isfinite(render).all()
    assert np.allclose(np.linalg.norm(quats, axis=1), 1, atol=1e-6)
    assert np.all(render[:, :3] >= np.array(bounds[:3])-1e-6) and np.all(render[:, :3] <= np.array(bounds[3:])+1e-6)
    assert np.array_equal(segments, np.concatenate([np.arange(a, b-1, dtype="<u4") for a, b in zip(offsets[:-1], offsets[1:])]))
    nodes, weights, lerps = (bindings[name][:, :3] for name in ("nodes", "weights", "lerps"))
    assert np.all(nodes+1 < nn) and np.all(nodes % 25 < 24)
    assert np.allclose(weights.sum(1), 1, atol=1e-6) and np.all(weights >= 0)
    assert np.all(lerps >= 0) and np.all(lerps <= 1)
    q = quats.reshape(ng, 25, 4)[:, :-1]
    tangent = np.diff(guides.reshape(ng, 25, 4)[:, :, :3], axis=1)
    length = np.linalg.norm(tangent, axis=2)
    assert np.all(length > 1e-8)
    tangent /= length[:, :, None]
    rotated_z = np.stack((2*(q[:,:,0]*q[:,:,2]+q[:,:,3]*q[:,:,1]), 2*(q[:,:,1]*q[:,:,2]-q[:,:,3]*q[:,:,0]), 1-2*(q[:,:,0]**2+q[:,:,1]**2)), axis=2)
    assert np.allclose(rotated_z, tangent, atol=3e-5)
    assert np.allclose(np.diff(guides.reshape(ng,25,4)[:,:,3],axis=1), length, atol=1e-6)
    reconstructed = sum((guides[nodes[:nn,k],:3]*(1-lerps[:nn,k,None]) + guides[nodes[:nn,k]+1,:3]*lerps[:nn,k,None])*weights[:nn,k,None] for k in range(3))
    assert np.allclose(reconstructed, render[:nn,:3], atol=1e-6)
    glb = (folder / "diana_head.glb").read_bytes()
    assert struct.unpack_from("<4sII", glb) == (b"glTF", 2, len(glb))
    js_len, js_tag = struct.unpack_from("<I4s", glb, 12)
    assert js_tag == b"JSON"
    document = json.loads(glb[20:20+js_len])
    payload_len, payload_tag = struct.unpack_from("<I4s", glb, 20+js_len)
    assert payload_tag == b"BIN\0"
    payload = glb[28+js_len:]
    assert len(payload) == payload_len
    def accessor(index):
        info = document["accessors"][index]
        view = document["bufferViews"][info["bufferView"]]
        n = {"VEC3": 3, "SCALAR": 1}[info["type"]]
        dtype = {5126: "<f4", 5125: "<u4"}[info["componentType"]]
        assert view["byteOffset"] + view["byteLength"] <= len(payload)
        return np.frombuffer(payload, dtype=dtype, count=info["count"]*n, offset=view["byteOffset"]+info.get("byteOffset",0)).reshape(-1,n)
    primitives = document["meshes"][0]["primitives"]
    head_positions = accessor(primitives[0]["attributes"]["POSITION"])
    head_indices = np.concatenate([accessor(p["indices"]).ravel() for p in primitives])
    assert head_positions.shape == (59280,3) and len(head_indices) == 79114*3
    assert np.max(head_indices) < len(head_positions)
    root_data = (folder / "diana_roots.bindingbin").read_bytes()
    assert struct.unpack_from("<8sII",root_data) == (b"DBIND001",1,ns)
    assert len(root_data) == 16+ns*24
    roots = np.frombuffer(root_data,dtype=[("triangle","<u4",3),("barycentric","<f4",3)],count=ns,offset=16)
    assert np.max(roots["triangle"]) < len(head_positions)
    root_surface = (head_positions[roots["triangle"]]*roots["barycentric"][:,:,None]).sum(1)
    distance = np.linalg.norm(root_surface-render[offsets[:-1],:3],axis=1)
    assert distance.max() < .001
    print(f"Portable Diana asset checks passed: {ng} guides, {nn} simulation nodes, {nv} render vertices, {len(head_indices)//3} head triangles")
    print(f"Actual scalp binding residual: mean {distance.mean()*1000:.6f} mm, max {distance.max()*1000:.6f} mm")


if __name__ == "__main__":
    validate(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[3] / "Asset/HairSimulation")
