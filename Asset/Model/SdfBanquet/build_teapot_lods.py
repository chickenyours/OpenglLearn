"""Audit the CC0 McGuire Utah Teapot and produce three actual mesh tiers.

The original file is preserved in source/teapot.obj. Position welding is only
for topology / subdivision; the importer can retain OBJ UV splits independently.
"""
from pathlib import Path
import collections
import json
import hashlib
import numpy as np
import sys
import shutil

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools"))


def read_obj(path):
    vertices, triangles = [], []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("v "):
            vertices.append([float(value) for value in line.split()[1:4]])
        elif line.startswith("f "):
            face = [int(value.split("/")[0]) - 1 for value in line.split()[1:]]
            for index in range(1, len(face) - 1):
                triangles.append([face[0], face[index], face[index + 1]])
    return np.asarray(vertices, dtype=np.float64), np.asarray(triangles, dtype=np.int32)


def weld(vertices, triangles):
    # Original ASCII positions have six decimal places; identical positions
    # are merged without changing the underlying geometry.
    _, first, mapping = np.unique(np.round(vertices, 6), axis=0,
                                  return_index=True, return_inverse=True)
    return vertices[first], mapping[triangles].astype(np.int32)


def mesh(vertices, triangles):
    crosses = np.cross(vertices[triangles[:, 1]] - vertices[triangles[:, 0]],
                       vertices[triangles[:, 2]] - vertices[triangles[:, 0]])
    triangles = triangles[np.linalg.norm(crosses, axis=1) > 1e-10]
    _, first = np.unique(np.sort(triangles, axis=1), axis=0, return_index=True)
    triangles = triangles[np.sort(first)]
    used, mapping = np.unique(triangles.reshape(-1), return_inverse=True)
    return vertices[used], mapping.reshape(-1, 3).astype(np.int32)


def audit(result):
    vertices, triangles = result
    edges = collections.Counter()
    oriented = collections.defaultdict(int)
    links = [collections.defaultdict(set) for _ in vertices]
    adjacency = [set() for _ in vertices]
    for triangle in triangles:
        for a, b in zip(triangle, np.roll(triangle, -1)):
            edge = tuple(sorted((int(a), int(b))))
            edges[edge] += 1
            oriented[edge] += 1 if a < b else -1
            adjacency[int(a)].add(int(b))
            adjacency[int(b)].add(int(a))
        for corner in range(3):
            a, b, c = (int(triangle[corner]), int(triangle[(corner + 1) % 3]),
                       int(triangle[(corner + 2) % 3]))
            links[a][b].add(c)
            links[a][c].add(b)
    nonmanifold_vertices = 0
    for link in links:
        visited, todo = set(), [next(iter(link))]
        while todo:
            at = todo.pop()
            if at in visited:
                continue
            visited.add(at)
            todo.extend(link[at] - visited)
        nonmanifold_vertices += any(len(neighbors) != 2 for neighbors in link.values()) or len(visited) != len(link)
    components, seen = 0, set()
    for vertex in range(len(vertices)):
        if vertex in seen:
            continue
        components += 1
        todo = [vertex]
        while todo:
            at = todo.pop()
            if at in seen:
                continue
            seen.add(at)
            todo.extend(adjacency[at] - seen)
    return {
        "vertices": len(vertices), "triangles": len(triangles),
        "boundary_edges": sum(count == 1 for count in edges.values()),
        "nonmanifold_edges": sum(count > 2 for count in edges.values()),
        "inconsistently_oriented_edges": sum(value != 0 for value in oriented.values()),
        "edge_manifold_closed": all(count == 2 for count in edges.values()),
        "nonmanifold_vertices": nonmanifold_vertices,
        "connected_components": components,
        "signed_volume": float(np.einsum("ij,ij->i", vertices[triangles[:, 0]],
                                         np.cross(vertices[triangles[:, 1]], vertices[triangles[:, 2]])).sum() / 6),
        "bounds_min": vertices.min(axis=0).tolist(),
        "bounds_max": vertices.max(axis=0).tolist(),
    }


def write_obj(path, vertices, triangles):
    crosses = np.cross(vertices[triangles[:, 1]] - vertices[triangles[:, 0]],
                       vertices[triangles[:, 2]] - vertices[triangles[:, 0]])
    normals = np.zeros_like(vertices)
    for corner in range(3):
        np.add.at(normals, triangles[:, corner], crosses)
    lengths = np.linalg.norm(normals, axis=1)
    normals /= np.maximum(lengths[:, None], 1e-30)
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write("# Modified Utah Teapot: CC0 McGuire / Kenzie Lamar source, QEM mesh LOD\n")
        output.write("# Position-welded, area-weighted smooth normals; no UV transfer.\n")
        output.write("o UtahTeapot\ns 1\n")
        for point in vertices:
            output.write("v " + " ".join(format(value, ".10g") for value in point) + "\n")
        for normal in normals:
            output.write("vn " + " ".join(format(value, ".10g") for value in normal) + "\n")
        for triangle in triangles + 1:
            output.write("f " + " ".join(f"{value}//{value}" for value in triangle) + "\n")


if __name__ == "__main__":
    source = ROOT / "source" / "teapot.obj"
    vertices, triangles = read_obj(source)
    vertices, triangles = weld(vertices, triangles)
    original = mesh(vertices, triangles)
    import fast_simplification
    report = {"source_url": "https://casual-effects.com/g3d/data10/common/model/teapot/teapot.zip",
              "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
              "model_license": "CC0", "simplifier": "fast-simplification 0.2.0 / QEM",
              "original": audit(original), "tiers": {}}
    shutil.copyfile(source, ROOT / "teapot_high.obj")
    report["tiers"]["high"] = audit(original)
    # Simplify independently from the unchanged source to prevent compounded
    # error between tiers. The source is y-up and is not normalized here.
    for name, target in (("medium", 7852), ("low", 3926)):
        points, faces = fast_simplification.simplify(*original, target_count=target, agg=7)
        simplified = mesh(points, faces)
        checked = audit(simplified)
        if not checked["edge_manifold_closed"] or checked["inconsistently_oriented_edges"] or checked["nonmanifold_vertices"]:
            raise RuntimeError(f"{name} simplification opened / invalidated the closed Utah Teapot")
        write_obj(ROOT / f"teapot_{name}.obj", *simplified)
        report["tiers"][name] = checked
    for name in report["tiers"]:
        report["tiers"][name]["file_sha256"] = hashlib.sha256((ROOT / f"teapot_{name}.obj").read_bytes()).hexdigest()
    (ROOT / "geometry-audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2), flush=True)
