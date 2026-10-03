opengl_add_portable_package(pixel_sandbox_demo DOCS
    "${CMAKE_SOURCE_DIR}/Module/PixelSandbox/README.md"
    "${CMAKE_SOURCE_DIR}/Module/PixelSandbox/ROADMAP.md"
    "${CMAKE_SOURCE_DIR}/Module/PixelSandbox/PERFORMANCE.md")
opengl_add_portable_package(forest_fire_demo DOCS "${CMAKE_SOURCE_DIR}/Module/ForestFire/README.md")
opengl_add_portable_package(terrain_render_demo)
set(_material_assets)
if(EXISTS "${CMAKE_SOURCE_DIR}/bin/materials/tite")
    list(APPEND _material_assets "${CMAKE_SOURCE_DIR}/bin/materials/tite|materials/tite")
endif()
opengl_add_portable_package(material_lab_demo ASSETS ${_material_assets}
    DOCS "${CMAKE_SOURCE_DIR}/Module/MaterialLab/README.md")
opengl_add_portable_package(brotato_game ASSETS "${CMAKE_SOURCE_DIR}/Asset/Brotato|Brotato"
    DOCS "${CMAKE_SOURCE_DIR}/Module/Brotato/README.md" "${CMAKE_SOURCE_DIR}/Module/Brotato/MIGRATION.md")
opengl_add_portable_package(iwanna_game ASSETS "${CMAKE_SOURCE_DIR}/Asset/IWanna|IWanna"
    COMPANIONS iwanna_showcase iwanna_editor iwanna_project iwanna_content iwanna_route_replay
    DOCS "${CMAKE_SOURCE_DIR}/Module/IWanna/EDITOR.md" "${CMAKE_SOURCE_DIR}/Module/IWanna/PREFAB_API.md")
