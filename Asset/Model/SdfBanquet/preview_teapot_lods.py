"""Local geometric QA view of the imported Utah Teapot tiers."""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from build_teapot_lods import read_obj

ROOT = Path(__file__).resolve().parent
figure = plt.figure(figsize=(13, 4.5))
for column, name in enumerate(("low", "medium", "high")):
    vertices, triangles = read_obj(ROOT / f"teapot_{name}.obj")
    # matplotlib uses Z up; the source/model importer uses Y up.
    vertices = vertices[:, [0, 2, 1]]
    axes = figure.add_subplot(1, 3, column + 1, projection="3d")
    surface = Poly3DCollection(vertices[triangles], linewidths=.04,
                               facecolor=(.72, .79, .87), edgecolor=(.3, .37, .44, .2))
    axes.add_collection3d(surface)
    axes.set_xlim(-85, 90)
    axes.set_ylim(-65, 65)
    axes.set_zlim(0, 90)
    axes.set_box_aspect((175, 130, 90))
    axes.view_init(elev=23, azim=-66)
    axes.set_title(f"{name.upper()} / {len(triangles)} triangles")
    axes.set_axis_off()
figure.suptitle("Utah Teapot / imported CC0 McGuire model", fontsize=14)
figure.tight_layout()
figure.savefig(ROOT / "teapot-lods-preview.png", dpi=150)
