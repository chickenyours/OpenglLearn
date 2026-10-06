set(MATERIAL_LAB_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_library(material_lab_module STATIC Private/material_lab_module.cpp Private/showcase_scene.cpp Private/gi_room_scene.cpp)
target_compile_features(material_lab_module PUBLIC cxx_std_20)
target_include_directories(material_lab_module PUBLIC
    ${MATERIAL_LAB_ROOT}/include
    ${MATERIAL_LAB_ROOT}/Engine/include
    ${MATERIAL_LAB_ROOT}/Engine/include/engine
    ${MATERIAL_LAB_ROOT}/Engine/include/engine/ToolAndAlgorithm
    ${MATERIAL_LAB_ROOT}/Module
    ${MATERIAL_LAB_ROOT}/Module/Render
    ${MATERIAL_LAB_ROOT}/Module/Render/Public
    ${MATERIAL_LAB_ROOT}/Module/Render/Private
)
set_target_properties(material_lab_module PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(material_lab_module PUBLIC render_effects opengl32 glfw3 glad)

add_executable(material_lab_demo Test/main.cpp)
target_link_libraries(material_lab_demo PRIVATE material_lab_module)

add_executable(material_lab_showcase_test Test/showcase_scene_test.cpp)
target_link_libraries(material_lab_showcase_test PRIVATE material_lab_module)
add_test(NAME material_lab_showcase_test COMMAND material_lab_showcase_test)

add_executable(material_lab_gi_room_test Test/gi_room_scene_test.cpp)
target_link_libraries(material_lab_gi_room_test PRIVATE material_lab_module)
add_test(NAME material_lab_gi_room_test COMMAND material_lab_gi_room_test)

add_executable(material_lab_geometry_test Test/lab_geometry_test.cpp)
target_include_directories(material_lab_geometry_test PRIVATE
    ${MATERIAL_LAB_ROOT}/include ${MATERIAL_LAB_ROOT}/Module)
add_test(NAME material_lab_geometry_test COMMAND material_lab_geometry_test)

add_executable(material_lab_pbr_test Test/pbr_material_test.cpp)
target_include_directories(material_lab_pbr_test PRIVATE
    ${MATERIAL_LAB_ROOT}/include ${MATERIAL_LAB_ROOT}/Module)
add_test(NAME material_lab_pbr_test COMMAND material_lab_pbr_test)

# Explicit opt-in: default CTest remains usable without a GPU/display.
option(MATERIAL_LAB_GPU_TESTS "Register the MaterialLab OpenGL 4.5 smoke test" OFF)
if(MATERIAL_LAB_GPU_TESTS)
    add_test(NAME material_lab_gi_room_gpu_test COMMAND material_lab_demo --gi-room-test --procedural)
    set_tests_properties(material_lab_gi_room_gpu_test PROPERTIES TIMEOUT 600 LABELS "gpu" RUN_SERIAL TRUE)
    add_test(NAME material_lab_showcase_motion_test COMMAND material_lab_demo --showcase-motion-test --procedural)
    set_tests_properties(material_lab_showcase_motion_test PROPERTIES TIMEOUT 600 LABELS "gpu" RUN_SERIAL TRUE)
    add_test(NAME material_lab_showcase_gpu_test COMMAND material_lab_demo --showcase-test --procedural)
    set_tests_properties(material_lab_showcase_gpu_test PROPERTIES TIMEOUT 240 LABELS "gpu" RUN_SERIAL TRUE)
    add_test(NAME material_lab_showcase_stability_test COMMAND material_lab_demo --showcase-stability-test --procedural)
    set_tests_properties(material_lab_showcase_stability_test PROPERTIES TIMEOUT 600 LABELS "gpu" RUN_SERIAL TRUE)
    add_test(NAME material_lab_lumen_view_stability_test COMMAND material_lab_demo --lumen-view-stability-test --procedural --benchmark-width 2560 --benchmark-height 1440)
    set_tests_properties(material_lab_lumen_view_stability_test PROPERTIES TIMEOUT 240 LABELS "gpu" RUN_SERIAL TRUE)
    add_test(NAME material_lab_lumen_gi_test COMMAND material_lab_demo --lumen-gi-test --procedural)
    set_tests_properties(material_lab_lumen_gi_test PROPERTIES TIMEOUT 240 LABELS "gpu")
    add_test(NAME material_lab_lumen_gi_stability_test COMMAND material_lab_demo --realtime-gi-stability-test --lumen-gi --procedural)
    set_tests_properties(material_lab_lumen_gi_stability_test PROPERTIES TIMEOUT 240 LABELS "gpu")
    add_test(NAME material_lab_realtime_gi_stability_test COMMAND material_lab_demo --realtime-gi-stability-test --procedural)
    set_tests_properties(material_lab_realtime_gi_stability_test PROPERTIES TIMEOUT 180 LABELS "gpu")
    add_test(NAME material_lab_realtime_gi_test COMMAND material_lab_demo --realtime-gi-test --procedural)
    set_tests_properties(material_lab_realtime_gi_test PROPERTIES TIMEOUT 180 LABELS "gpu")
    add_test(NAME material_lab_probe_test COMMAND material_lab_demo --probe-test --procedural)
    set_tests_properties(material_lab_probe_test PROPERTIES TIMEOUT 180 LABELS "gpu")
    add_test(NAME material_lab_sky_test COMMAND material_lab_demo --sky-test --procedural)
    set_tests_properties(material_lab_sky_test PROPERTIES TIMEOUT 180 LABELS "gpu")
    add_test(NAME material_lab_gpu_test COMMAND material_lab_demo --smoke-test)
    set_tests_properties(material_lab_gpu_test PROPERTIES TIMEOUT 60 LABELS "gpu")
    add_test(NAME material_lab_quality_test COMMAND material_lab_demo --quality-test --procedural)
    set_tests_properties(material_lab_quality_test PROPERTIES TIMEOUT 60 LABELS "gpu")
    add_test(NAME material_lab_shadow_test COMMAND material_lab_demo --shadow-test --procedural)
    set_tests_properties(material_lab_shadow_test PROPERTIES TIMEOUT 60 LABELS "gpu")
    add_test(NAME material_lab_closeup_test COMMAND material_lab_demo --closeup-test --procedural)
    set_tests_properties(material_lab_closeup_test PROPERTIES TIMEOUT 120 LABELS "gpu")
    add_test(NAME material_lab_advanced_test COMMAND material_lab_demo --advanced-test --procedural)
    set_tests_properties(material_lab_advanced_test PROPERTIES TIMEOUT 120 LABELS "gpu")
    add_test(NAME material_lab_advanced_quality_test COMMAND material_lab_demo --advanced-quality-test --procedural)
    set_tests_properties(material_lab_advanced_quality_test PROPERTIES TIMEOUT 180 LABELS "gpu")
endif()
