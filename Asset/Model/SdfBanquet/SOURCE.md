# Utah Teapot source assets

The banquet uses the historic **Utah Teapot**, originally modeled by Martin
Newell, in the repaired version by Kenzie Lamar archived by Morgan McGuire.
This is an imported OBJ asset, not a procedural approximation of a teapot.

- Archive: [McGuire Computer Graphics Archive](https://casual-effects.com/g3d/data10/index.html).
- Exact metadata: [teapot/info.js](https://casual-effects.com/g3d/data10/common/model/teapot/info.js).
- Original download: [teapot.zip](https://casual-effects.com/g3d/data10/common/model/teapot/teapot.zip).
- Downloaded on 2026-10-14. The archive marks this model **CC0**; the full
  [CC0 legal text](LICENSE-CC0.txt) is retained here.
- Cite: Morgan McGuire, Computer Graphics Archive, July 2017,
  <https://casual-effects.com/data>.

The archive credits Martin Newell and describes Kenzie Lamar's repair of the
lid, inner shell, spout and handle to form a manifold without boundary. The
unmodified source OBJ, material, texture, original ZIP and metadata snapshot
are retained in this directory. `teapot_high.obj` is byte-identical to
`source/teapot.obj`, including its original UVs and normals.

## Actual raster mesh tiers

| File | Triangles | Welded position vertices | Method |
|---|---:|---:|---|
| `teapot_low.obj` | 3926 | 1961 | Independent quadric error edge collapse from the original |
| `teapot_medium.obj` | 7852 | 3924 | Independent quadric error edge collapse from the original |
| `teapot_high.obj` | 15704 | 7850 | Original McGuire OBJ geometry |

The source OBJ has 8478 position entries before coincident seam positions are
welded for topology analysis. The simplified files contain recomputed
area-weighted smooth normals and no transferred UVs. The renderer can assign
its own materials. These two derived tiers are **Modified Utah Teapots**;
they reduce actual geometry rather than increasing triangle counts by linear
subdivision. SDF grid resolution is a separate setting from raster mesh LOD.

The files are Y-up and preserve the source coordinate scale. High bounds are
`(-71.892532, 0, -47.928356)` to
`(82.293999, 79.006561, 47.928356)`. Runtime normalization should use a common
reference transform derived from HIGH for all three tiers so geometry does
not jump when LOD changes.

## Geometry verification

`build_teapot_lods.py` welds identical source positions, filters degenerate
triangles and audits each tier. All three have:

- 0 boundary edges and 0 edges incident to more than two triangles;
- 0 inconsistent oriented edge pairs;
- 0 non-manifold vertex links;
- 2 connected, closed surface components, retained across simplification.

This supports closed-mesh signed-distance construction. It is a topology
audit, not an exhaustive proof of absence of geometric self-intersections.
The historical disjoint Bézier/freeglut teapot is a different source and is
not used here; its open spout and separated patches must not be described as
the same closed mesh.

The detailed counts, bounds, signed volumes and SHA-256 hashes are in
[geometry-audit.json](geometry-audit.json). HIGH source SHA-256 is
`6e5d123af85bcbf52a8c9da1ff970b78eda41a9188418a1cb6b8b3696a683a7e`.

## Reproduction tool

The derivative tiers were generated with
[PyVista fast-simplification 0.2.0](https://github.com/pyvista/fast-simplification),
using target counts 7852 / 3926 and QEM aggressiveness 7. Its isolated
CPython 3.10 Windows tool files are under `tools/`, with
[MIT license](LICENSE-simplifier-MIT.txt). They are preparation tools and are
not a runtime dependency of the C++ renderer.

```text
python Asset/Model/SdfBanquet/build_teapot_lods.py
```

The HIGH file is copied unchanged; LOW and MEDIUM are independently derived
from the original, then verified before export. No geometry smoothing,
surface subdivision or fabricated teapot geometry is involved.
