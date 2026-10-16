"""""" # start delvewheel patch
def _delvewheel_patch_1_13_0():
    import os
    if os.path.isdir(libs_dir := os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir, 'fast_simplification.libs'))):
        os.add_dll_directory(libs_dir)


_delvewheel_patch_1_13_0()
del _delvewheel_patch_1_13_0
# end delvewheel patch

from importlib.metadata import PackageNotFoundError
from importlib.metadata import version as _version

from .replay import _map_isolated_points, replay_simplification  # noqa: F401
from .simplify import simplify, simplify_mesh  # noqa: F401

try:
    __version__ = _version("fast_simplification")
except PackageNotFoundError:  # pragma: no cover
    __version__ = "0.0.0"
