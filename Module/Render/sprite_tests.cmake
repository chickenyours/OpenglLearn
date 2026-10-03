add_library(render_sprite2d STATIC
    ${CMAKE_CURRENT_LIST_DIR}/Private/Sprite/sprite_batch.cpp
    ${CMAKE_SOURCE_DIR}/include/stb_image.cpp)
target_compile_features(render_sprite2d PUBLIC cxx_std_20)
target_include_directories(render_sprite2d PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/Public ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/Private
    ${CMAKE_SOURCE_DIR}/Module/Render ${CMAKE_SOURCE_DIR}/Module/Render/Public ${CMAKE_SOURCE_DIR}/Module/Render/Private)
target_link_libraries(render_sprite2d PUBLIC opengl32 glfw3 glad)
set_target_properties(render_sprite2d PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_executable(sprite_batch_geometry_test ${CMAKE_CURRENT_LIST_DIR}/Test/sprite_batch_geometry_test.cpp)
target_include_directories(sprite_batch_geometry_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME sprite_batch_geometry_test COMMAND sprite_batch_geometry_test)
add_executable(sprite_animation_test ${CMAKE_CURRENT_LIST_DIR}/Test/sprite_animation_test.cpp)
target_include_directories(sprite_animation_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME sprite_animation_test COMMAND sprite_animation_test)
add_executable(sprite_batch_runtime_test ${CMAKE_CURRENT_LIST_DIR}/Test/sprite_batch_runtime_test.cpp)
target_link_libraries(sprite_batch_runtime_test PRIVATE render_sprite2d)
add_test(NAME sprite_batch_runtime_test COMMAND sprite_batch_runtime_test)

add_executable(texture_update_test ${CMAKE_CURRENT_LIST_DIR}/Test/texture_update_test.cpp)
target_link_libraries(texture_update_test PRIVATE render_sprite2d)
add_test(NAME texture_update_test COMMAND texture_update_test)
