#!/usr/bin/env python3
"""Generate an explicit Alice high-heel corrective morph from a native cache.

This is an Alice-specific authoring utility, not an automatic general-purpose
shoe fitter. NumPy is required only when generating the profile; the editor
does not need Python or NumPy and does not invoke this script.

Example (run from the repository root):
  python Module/Model/Tools/fit_alice_shoes.py \
    --package Asset/Generated/AliceMaid_FitFix_20261016 \
    --metadata build/alice_fit_export_final/source.blend.json \
    --output build/AliceMaid.shoesfit.reproduced.json \
    --report build/alice_shoesfit.reproduced.report.json

The saved body-only parameters reproduce the GPU-reviewed r6 correction.
The source stockings already fit the shoes and remain unchanged. This utility never
opens Blender or changes the source, cached geometry, skeleton or SourcePose.
Distances and offsets below use source units; they are not millimeters.
"""
import os
os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["OMP_NUM_THREADS"] = "1"
import argparse
import json
import mmap
from pathlib import Path
import numpy as np
OUTSIDE_TOLERANCE = 0.002  # source units, without a physical unit conversion


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


def trs(value):
    q = np.asarray(value["rotation"], dtype=np.float64)
    q /= np.linalg.norm(q)
    x, y, z, w = q
    result = np.eye(4)
    result[:3, :3] = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
    result[:3, :3] *= np.asarray(value["scale"])[None, :]
    result[:3, 3] = value["translation"]
    return result


def smooth(value):
    t = np.clip(value, 0, 1)
    return t*t*(3-2*t)


def integral_ramp(y):
    t = np.clip((y-.25)/.15, 0, 1)
    return np.where(y >= .4, y-.325, .15*(t**3-.5*t**4))


def warp_positions(points, side):
    result = points.copy()
    domain = smooth((.60-np.abs(points[:, 0]))/.25) * smooth((1.5-points[:, 1])/.4) * smooth((1.3-points[:, 2])/.3)
    result[:, 1] -= .59*integral_ramp(points[:, 1])*domain
    front = smooth((points[:, 2]+.02)/.25) * smooth((points[:, 1]-.30)/.30) * domain
    result[:, 2] += front*(.32-.5*points[:, 2])
    result[:, 0] += front*(-.20*points[:, 0]+(.030 if side == "L" else -.030))
    return result


def warp(points, side):
    result = warp_positions(points, side)
    jacobian = np.zeros((len(points), 3, 3))
    epsilon = 1e-5
    for axis in range(3):
        step = np.zeros(3)
        step[axis] = epsilon
        jacobian[:, :, axis] = (warp_positions(points+step, side)-warp_positions(points-step, side))/(2*epsilon)
    return result, jacobian


class NativeAlice:
    def __init__(self, package, metadata):
        manifest = package if package.is_file() else package/"Package.asset.json"
        self.asset, self.metadata_path, self.metadata = read(manifest), metadata, read(metadata)
        self.geometry_path = manifest.parent/self.asset["skeletalMesh"]
        self.geometry = read(self.geometry_path)
        self.bones = read(manifest.parent/self.asset["skeleton"])["bones"]
        self.names = [bone["name"] for bone in self.bones]
        clips = [read(manifest.parent/path) for path in self.asset["animations"]]
        source = next(clip for clip in clips if clip["name"] == "SourcePose" and clip["generated"])
        local = [dict(bone["restLocal"]) for bone in self.bones]
        for track in source["boneTracks"]:
            local[track["bone"]] = {key: track[field][0]["value"] for key, field in
                (("translation", "translations"), ("rotation", "rotations"), ("scale", "scales"))}
        globals_ = {}
        def global_bone(index):
            if index not in globals_:
                parent = self.bones[index]["parent"]
                globals_[index] = (global_bone(parent) if parent >= 0 else np.eye(4)) @ trs(local[index])
            return globals_[index]
        self.pose = np.array([global_bone(i) for i in range(len(self.bones))])
        self.foot = {side: self.pose[self.names.index("Bip01 "+side+" Foot")] for side in ("L", "R")}
        self.inverse_foot = {side: np.linalg.inv(m) for side, m in self.foot.items()}
        self.stream = (self.geometry_path.parent/self.geometry["payload"]).open("rb")
        self.binary = mmap.mmap(self.stream.fileno(), 0, access=mmap.ACCESS_READ)
        assert self.geometry["byteOrder"] == "little" and self.geometry["vertexStride"] == 60
        self.meshes = []
        for region in self.asset["meshRegions"]:
            if region["sourceObject"] != "body":
                continue
            mesh = self.read_mesh(region["mesh"])
            mesh["object"] = region["sourceObject"]
            self.meshes.append(mesh)
            print("Native section", region["mesh"], region["sourceObject"], len(mesh["position"]), flush=True)

    def array(self, descriptor, dtype, width=1, field_offset=0):
        item = np.dtype(dtype)
        return np.ndarray((descriptor["count"], width), dtype=item, buffer=self.binary,
            offset=descriptor["offset"]+field_offset,
            strides=(descriptor["stride"], item.itemsize)).copy()

    def read_mesh(self, index):
        mesh = self.geometry["meshes"][index]
        assert np.max(np.abs(np.asarray(mesh["bindTransform"])-np.eye(4))) < 1e-8
        position = self.array(mesh["vertices"], "<f8", 3)
        normal = self.array(mesh["vertices"], "<f4", 3, 24)
        if "fullInfluenceOffsets" in mesh:
            offsets = self.array(mesh["fullInfluenceOffsets"], "<u4").ravel().astype(np.int64)
            assert len(offsets) == len(position)+1 and offsets[0] == 0 and np.all(np.diff(offsets)>0)
            descriptor = mesh["fullInfluences"]
            assert offsets[-1] == descriptor["count"]
            joint = self.array(descriptor, "<u4").ravel()
            weight = self.array(descriptor, "<f4", field_offset=4).ravel().astype(np.float64)
            row = np.repeat(np.arange(len(position)), np.diff(offsets))
        else:
            joint = self.array(mesh["skin"], "<u4", 4).ravel()
            weight = self.array(mesh["skin"], "<f4", 4, 16).ravel().astype(np.float64)
            row = np.repeat(np.arange(len(position)), 4)
        positive = weight > 0
        row, joint, weight = row[positive], joint[positive], weight[positive]
        assert np.all(joint < len(self.bones)) and np.all(np.isfinite(weight))
        totals = np.bincount(row, weights=weight, minlength=len(position))
        assert np.all(totals > 0)
        weight /= totals[row]
        offsets = np.asarray(mesh["boneFromMesh"]).transpose(0, 2, 1)
        matrices = self.pose @ offsets
        blended = np.zeros((len(position), 4, 4))
        np.add.at(blended, row, matrices[joint]*weight[:, None, None])
        world = np.einsum("nij,nj->ni", blended[:, :3, :3], position)+blended[:, :3, 3]
        side_weight, toe_weight = {}, {}
        for side in ("L", "R"):
            foot = "Bip01 "+side+" Foot"
            toe = "Bip01 "+side+" Toe"
            selected = np.array([name == foot or name.startswith(toe) for name in self.names])
            toes = np.array([name.startswith(toe) for name in self.names])
            side_weight[side] = np.bincount(row, weights=weight*selected[joint], minlength=len(position))
            toe_weight[side] = np.bincount(row, weights=weight*toes[joint], minlength=len(position))
        return dict(mesh=index, position=position, normal=normal, blended=blended,
                    world=world, sideWeight=side_weight, toeWeight=toe_weight)

    def collected(self, side):
        result = []
        for mesh in self.meshes:
            inverse = self.inverse_foot[side]
            local = mesh["world"]@inverse[:3, :3].T+inverse[:3, 3]
            selected = np.linalg.norm(warp_positions(local, side)-local, axis=1)>1e-10
            if np.any(selected):
                result.append((mesh, selected, local[selected], mesh["toeWeight"][side][selected]))
        return result

    def shoe_triangles(self, side):
        triangles = []
        inverse = self.inverse_foot[side]
        for region in self.asset["meshRegions"]:
            if region["sourceObject"] != "shoes" or region["sourceMaterial"] != "highheel":
                continue
            mesh = self.read_mesh(region["mesh"])
            local = mesh["world"]@inverse[:3, :3].T+inverse[:3, 3]
            indices = self.array(self.geometry["meshes"][region["mesh"]]["indices"], "<u4").ravel().reshape(-1, 3)
            values = local[indices]
            triangles.append(values[np.all(np.abs(values[:, :, 0]) < .4, axis=1)])
        assert triangles, "Expected Alice highheel source-material provenance"
        return np.concatenate(triangles)


class ShoeSurfaces:
    """Actual blue-shoe shell ray intervals, excluding its dark outsole.

    The local Y direction points toward the sole. Solidify gives paired
    outer/inner crossings; the penultimate crossing is the inner sole top.
    A preceding pair can be the toe cap or a strap. Accordingly, cap metrics
    are diagnostics and cannot classify every ray as a closed toe cavity.
    """
    def __init__(self, native, side):
        self.triangles = native.shoe_triangles(side)
        self.origin, self.step, self.size = np.array([-.25, -.20]), .025, np.array([21, 43])
        self.cells = {}
        projection = self.triangles[:, :, [0, 2]]
        lo = np.floor((projection.min(1)-self.origin)/self.step).astype(int)
        hi = np.floor((projection.max(1)-self.origin)/self.step).astype(int)
        for index in range(len(projection)):
            low, high = np.maximum(lo[index], 0), np.minimum(hi[index], self.size-1)
            for x in range(low[0], high[0]+1):
                for z in range(low[1], high[1]+1):
                    self.cells.setdefault((x, z), []).append(index)

    def query(self, points):
        cap = np.full(len(points), np.nan)
        sole = cap.copy()
        cells = np.floor((points-self.origin)/self.step).astype(int)
        for cell in np.unique(cells, axis=0):
            triangle_ids = self.cells.get(tuple(cell), [])
            if not triangle_ids:
                continue
            ids = np.flatnonzero(np.all(cells == cell, axis=1))
            triangles = self.triangles[triangle_ids]
            v0 = triangles[:, 0][:, [0, 2]]
            u, v = triangles[:, 1][:, [0, 2]]-v0, triangles[:, 2][:, [0, 2]]-v0
            det = u[:, 0]*v[:, 1]-u[:, 1]*v[:, 0]
            valid = np.abs(det)>1e-16
            denominator = np.where(valid, det, 1)
            w = points[ids, None, :]-v0
            b = (w[:, :, 0]*v[:, 1]-w[:, :, 1]*v[:, 0])/denominator
            c = (u[:, 0]*w[:, :, 1]-u[:, 1]*w[:, :, 0])/denominator
            inside = valid & (b>=-1e-8) & (c>=-1e-8) & (b+c<=1+1e-8)
            y = triangles[:, 0, 1]+b*(triangles[:, 1, 1]-triangles[:, 0, 1])+c*(triangles[:, 2, 1]-triangles[:, 0, 1])
            for k, index in enumerate(ids):
                values = np.unique(np.round(y[k, inside[k]], 7))
                values = values[(values>.25) & (values<.8)]
                if len(values)>=2:
                    sole[index] = values[-2]
                if len(values)>=4:
                    cap[index] = values[-3]
        return cap, sole


def generate(native):
    deltas, reports, used_vertices = [], [], set()
    minimum_jacobian, maximum_move = 1., 0.
    new_world = {mesh["mesh"]: mesh["world"].copy() for mesh in native.meshes}
    for side in ("L", "R"):
        surfaces = ShoeSurfaces(native, side)
        for mesh, selected, local, toe_weight in native.collected(side):
            corrected, jacobian = warp(local, side)
            change = corrected-local
            world_delta = change@native.foot[side][:3, :3].T
            blended = mesh["blended"][selected, :3, :3]
            bind_delta = np.linalg.solve(blended, world_delta)
            bind_jacobian = np.linalg.solve(blended,
                native.foot[side][:3, :3]@jacobian@native.inverse_foot[side][:3, :3]@blended)
            normals = mesh["normal"][selected].astype(np.float64)
            new_normal = np.linalg.solve(bind_jacobian.transpose(0, 2, 1), normals)
            new_normal /= np.maximum(np.linalg.norm(new_normal, axis=1)[:, None], 1e-12)
            minimum_jacobian = min(minimum_jacobian, float(np.linalg.det(jacobian).min()))
            maximum_move = max(maximum_move, float(np.linalg.norm(change, axis=1).max()))
            for k, index in enumerate(np.flatnonzero(selected)):
                if np.linalg.norm(bind_delta[k]) < 1e-10:
                    continue
                key = (mesh["mesh"], int(index))
                assert key not in used_vertices, "Left/right correction domains overlap"
                used_vertices.add(key)
                deltas.append(dict(mesh=mesh["mesh"], vertex=int(index),
                    expectedPosition=mesh["position"][index].tolist(),
                    positionDelta=bind_delta[k].tolist(), normalDelta=(new_normal[k]-normals[k]).tolist()))
            new_world[mesh["mesh"]][selected] += world_delta
            inverse = native.inverse_foot[side]
            original = mesh["world"]@inverse[:3, :3].T+inverse[:3, 3]
            changed = warp_positions(original, side)
            inspect = (mesh["sideWeight"][side]>.15) & (original[:, 1]>.25) & (original[:, 2]>-.2) & (np.abs(original[:, 0])<.30)
            for name, mask in [("wholeFoot", inspect), ("frontFoot", inspect & (original[:, 2]>.20)),
                               ("toe", inspect & (original[:, 1]>.65) & (original[:, 2]>.32))]:
                points = changed[mask]
                cap, sole = surfaces.query(points[:, [0, 2]])
                valid, closed = np.isfinite(sole), np.isfinite(cap)
                below, above = np.maximum(points[:, 1]-sole, 0), np.maximum(cap-points[:, 1], 0)
                report = dict(side=side, mesh=mesh["mesh"], object=mesh["object"], region=name,
                    count=len(points), soleCovered=int(valid.sum()), capCovered=int(closed.sum()),
                    maxBelowSole=float(np.nanmax(below)) if np.any(valid) else 0,
                    maxAboveCap=float(np.nanmax(above)) if np.any(closed) else 0,
                    belowCount=int(np.count_nonzero(below>OUTSIDE_TOLERANCE)),
                    aboveCount=int(np.count_nonzero(above>OUTSIDE_TOLERANCE)))
                reports.append(report)
                print(json.dumps(report), flush=True)
    triangles = []
    for mesh in native.meshes:
        indices = native.array(native.geometry["meshes"][mesh["mesh"]]["indices"], "<u4").ravel().reshape(-1, 3)
        old, new = mesh["world"][indices], new_world[mesh["mesh"]][indices]
        before = np.cross(old[:, 1]-old[:, 0], old[:, 2]-old[:, 0])
        after = np.cross(new[:, 1]-new[:, 0], new[:, 2]-new[:, 0])
        before_length, after_length = np.linalg.norm(before, axis=1), np.linalg.norm(after, axis=1)
        valid = before_length>1e-12
        cosine = np.einsum("ni,ni->n", before[valid], after[valid])/np.maximum(before_length[valid]*after_length[valid], 1e-30)
        check = dict(mesh=mesh["mesh"], reversedNormalCount=int(np.count_nonzero(cosine<0)),
            newDegenerateCount=int(np.count_nonzero(valid & (after_length<=1e-12))),
            minAreaRatio=float(np.min(after_length[valid]/before_length[valid])), minNormalCosine=float(cosine.min()))
        triangles.append(check)
        assert check["reversedNormalCount"] == 0 and check["newDegenerateCount"] == 0
    assert minimum_jacobian > 0 and all(np.isfinite(d["positionDelta"]).all() for d in deltas)
    report = dict(candidateRevision="r6_body_only", method="GPU-reviewed r6: exact r4 body-only correction; source stockings identity",
        distanceUnits="source units", outsideToleranceSourceUnits=OUTSIDE_TOLERANCE,
        parameters=dict(soleLiftIntegralCoefficient=.59, integralTransitionY=[.25, .40],
            frontTransitionY=[.30, .60], frontTransitionZ=[-.02, .23],
            frontZTarget="0.32+0.5*sourceZ", frontXTarget="0.8*sourceX +/- 0.030"),
        minimumJacobianDeterminant=minimum_jacobian, maximumFootSpaceDisplacement=maximum_move,
        morphDeltaCount=len(deltas), stockingsCorrectionCount=0, surfaceChecks=reports, triangleChecks=triangles)
    profile = dict(assetType="SkeletalFitProfile", version=1, sourceAssetId=native.asset["id"],
        baseClip="SourcePose", name="ShoesFit", boneCorrections=[], morphDeltas=deltas,
        notes=["Explicit independent Alice high-heel fit corrective morph; original geometry, bind and SourcePose remain unchanged.",
               "Only the bare body is fitted. Source stockings already follow the shoe last and remain unchanged.",
               "Native expectedPosition values guard exact mesh/vertex provenance; full blended skin matrices map world corrections back into bind space.",
               "GPU r6 near-camera review passed. Open-instep, strap-ray and rear-foot metric residuals remain documented; this does not validate every future animation."],
        evidence=report)
    return profile, report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--package", type=Path, required=True, help="Native Alice cache directory or package manifest")
    parser.add_argument("--metadata", type=Path, required=True, help="Exact import source.blend.json with original shoe point-cloud reference")
    parser.add_argument("--output", type=Path, required=True, help="New SkeletalFitProfile JSON destination")
    parser.add_argument("--report", type=Path, help="Optional separate metrics JSON destination")
    args = parser.parse_args()
    for path in (args.output, args.report):
        if path and path.exists():
            parser.error("Use a new output path; refusing to replace an existing file: "+str(path))
    profile, report = generate(NativeAlice(args.package.resolve(), args.metadata.resolve()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(profile, ensure_ascii=False, separators=(",", ":"), allow_nan=False), encoding="utf-8")
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
    print("Generated", args.output, "with", len(profile["morphDeltas"]), "explicit vertex corrections")


if __name__ == "__main__":
    main()
