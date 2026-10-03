# Lua dependency

Vendored Lua 5.4.9, statically compiled as C by `Module/Scripting/CMakeLists.txt`.
Official archive: https://www.lua.org/ftp/lua-5.4.9.tar.gz
SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6` (verified before extraction).

Original source, documentation and license are retained in `lua-5.4.9`.
See `lua-5.4.9/doc/readme.html` for the MIT license. No Lua interpreter/DLL
installation or network access is needed to build or run after checkout.
Only the VM library is built, not the standalone `lua`/`luac` tools.

# Shared Release / Debug dependencies

`cmake/Dependencies.cmake` builds the following pinned sources rather than
reusing the old `/MDd` libraries under `lib/debug`. No downloads, Python, or
generator steps are needed during normal CMake builds.

| Source directory | Upstream archive | SHA-256 |
| --- | --- | --- |
| `glfw-3.4` | https://codeload.github.com/glfw/glfw/zip/refs/tags/3.4 | `a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a` |
| `glad-0.1.36` | https://codeload.github.com/Dav1dde/glad/zip/refs/tags/v0.1.36 | `3b407840e2c3ab7896486fe7d2f0324f83f01f943b1b723e79ae4e775e63322b` |
| `jsoncpp-1.9.5` | https://codeload.github.com/open-source-parsers/jsoncpp/zip/refs/tags/1.9.5 | `a074e1b38083484e8e07789fd683599d19da8bb960959c83751cd0284bdf2043` |

The versions match the existing GLFW 3.4, GLAD 0.1.36 OpenGL 4.6 core, and
JsonCpp 1.9.5 public headers. Original upstream licenses are retained; portable
packages collect notices from `cmake/licenses`. GLM's omitted license was
restored from https://raw.githubusercontent.com/g-truc/glm/0.9.9.8/copying.txt,
matching the checked-in 0.9.9.8 headers.

GLAD's C implementation is checked into `glad-generated/src/glad.c`. It was
generated offline from the pinned generator's bundled registry using Python:

```powershell
cd third_party/glad-0.1.36
python -m glad --profile core --api gl=4.6 --generator c --spec gl --extensions= --reproducible --out-path ../glad-generated
```

The build compiles that generated file against `include/glad/glad.h`, preserving
the project's existing API. The generated header/KHR files are retained for
provenance. JsonCpp is built directly from its three `src/lib_json` translation
units; its amalgamation generator is not part of the build.
