# Lua dependency

Vendored Lua 5.4.9, statically compiled as C by `Module/Scripting/CMakeLists.txt`.
Official archive: https://www.lua.org/ftp/lua-5.4.9.tar.gz
SHA-256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6` (verified before extraction).

Original source, documentation and license are retained in `lua-5.4.9`.
See `lua-5.4.9/doc/readme.html` for the MIT license. No Lua interpreter/DLL
installation or network access is needed to build or run after checkout.
Only the VM library is built, not the standalone `lua`/`luac` tools.
