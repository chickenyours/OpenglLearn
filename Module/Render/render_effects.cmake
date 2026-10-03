add_library(render_effects STATIC Render/Private/Pipeline/material_render_pipeline.cpp)
target_compile_features(render_effects PUBLIC cxx_std_20)
target_include_directories(render_effects PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/Render
    ${CMAKE_SOURCE_DIR}/Module/Render/Public
    ${CMAKE_SOURCE_DIR}/Module/Render/Private)
target_link_directories(render_effects PUBLIC ${CMAKE_SOURCE_DIR}/lib/debug)
target_link_libraries(render_effects PUBLIC opengl32 glfw3 glad)
set_target_properties(render_effects PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Render)

add_executable(render_target_test Render/Test/render_target_test.cpp)
target_link_libraries(render_target_test PRIVATE render_effects)
add_test(NAME render_target_test COMMAND render_target_test)

add_executable(scene_effects_test Render/Test/scene_effects_test.cpp)
target_include_directories(scene_effects_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME scene_effects_test COMMAND scene_effects_test)

add_executable(post_process_test Render/Test/post_process_test.cpp)
target_include_directories(post_process_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME post_process_test COMMAND post_process_test)

add_executable(material_pipeline_lifecycle_test Render/Test/material_pipeline_lifecycle_test.cpp)
target_link_libraries(material_pipeline_lifecycle_test PRIVATE render_effects)
add_test(NAME material_pipeline_lifecycle_test COMMAND material_pipeline_lifecycle_test)
set_tests_properties(material_pipeline_lifecycle_test PROPERTIES TIMEOUT 10)
