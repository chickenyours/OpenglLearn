get_target_property(IWANNA_INCLUDES terrain_render_demo INCLUDE_DIRECTORIES)
set(IWANNA_ECS_SOURCES
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp)
add_library(iwanna_module STATIC Private/content_format.cpp Private/game_module.cpp Private/game_config.cpp Private/alpha_mask.cpp Private/tile_map.cpp Private/room_data.cpp Private/room_world.cpp ${IWANNA_ECS_SOURCES})
target_link_libraries(iwanna_module PUBLIC jsonloader scripting_module)
target_include_directories(iwanna_module PUBLIC ${IWANNA_INCLUDES})
add_executable(iwanna_game main.cpp Private/sprite_renderer.cpp)
target_link_libraries(iwanna_game PRIVATE iwanna_module audio_module opengl32 glfw3 glad)
add_custom_target(iwanna_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Asset/IWanna ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/IWanna
    COMMENT "Copying extracted IWanna assets")
add_dependencies(iwanna_game iwanna_assets)
add_executable(iwanna_showcase main.cpp Private/sprite_renderer.cpp)
target_compile_definitions(iwanna_showcase PRIVATE IWANNA_SHOWCASE=1)
target_link_libraries(iwanna_showcase PRIVATE iwanna_module audio_module opengl32 glfw3 glad)
add_dependencies(iwanna_showcase iwanna_assets)
add_executable(iwanna_game_test Test/game_test.cpp)
target_link_libraries(iwanna_game_test PRIVATE iwanna_module audio_module)
target_compile_definitions(iwanna_game_test PRIVATE IWANNA_ASSETS="${CMAKE_SOURCE_DIR}/Asset/IWanna")
add_test(NAME iwanna_game_test COMMAND iwanna_game_test)
add_executable(iwanna_room_test Test/room_test.cpp)
target_link_libraries(iwanna_room_test PRIVATE iwanna_module)
target_compile_definitions(iwanna_room_test PRIVATE IWANNA_ASSETS="${CMAKE_SOURCE_DIR}/Asset/IWanna")
add_test(NAME iwanna_room_test COMMAND iwanna_room_test)
add_executable(iwanna_editor editor_main.cpp Private/sprite_renderer.cpp)
target_link_libraries(iwanna_editor PRIVATE iwanna_module opengl32 glfw3 glad)
add_dependencies(iwanna_editor iwanna_assets)
add_executable(iwanna_editor_test Test/editor_test.cpp)
target_link_libraries(iwanna_editor_test PRIVATE iwanna_module)
target_compile_definitions(iwanna_editor_test PRIVATE IWANNA_ASSETS="${CMAKE_SOURCE_DIR}/Asset/IWanna")
add_test(NAME iwanna_editor_test COMMAND iwanna_editor_test)
add_executable(iwanna_content content_main.cpp)
target_link_libraries(iwanna_content PRIVATE iwanna_module)
add_executable(iwanna_topology_test Test/topology_test.cpp)
target_link_libraries(iwanna_topology_test PRIVATE iwanna_module)
target_compile_definitions(iwanna_topology_test PRIVATE IWANNA_ASSETS="${CMAKE_SOURCE_DIR}/Asset/IWanna")
add_test(NAME iwanna_topology_test COMMAND iwanna_topology_test)
