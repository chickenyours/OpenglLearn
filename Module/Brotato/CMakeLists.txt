# Headless gameplay and reusable RHI sprite rendering are separate libraries.
set(BROTATO_ECS_SOURCES
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp)
add_library(brotato_module STATIC Private/game_module.cpp Private/weapons.cpp Private/presentation.cpp Private/session_module.cpp ${BROTATO_ECS_SOURCES})
target_compile_features(brotato_module PUBLIC cxx_std_20)
target_include_directories(brotato_module PUBLIC
    ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module)
set_target_properties(brotato_module PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(brotato_module PUBLIC Threads::Threads)
endif()
add_executable(brotato_game_test Test/game_test.cpp)
target_link_libraries(brotato_game_test PRIVATE brotato_module)
add_test(NAME brotato_game_test COMMAND brotato_game_test)
add_executable(brotato_weapon_test Test/weapon_test.cpp)
target_link_libraries(brotato_weapon_test PRIVATE brotato_module)
add_test(NAME brotato_weapon_test COMMAND brotato_weapon_test)

add_executable(brotato_session_test Test/session_test.cpp)
target_link_libraries(brotato_session_test PRIVATE brotato_module)
add_test(NAME brotato_session_test COMMAND brotato_session_test)

add_executable(brotato_menu_layout_test Test/menu_layout_test.cpp)
target_link_libraries(brotato_menu_layout_test PRIVATE brotato_module)
add_test(NAME brotato_menu_layout_test COMMAND brotato_menu_layout_test)

add_executable(brotato_presentation_test Test/presentation_test.cpp)
target_link_libraries(brotato_presentation_test PRIVATE brotato_module)
add_test(NAME brotato_presentation_test COMMAND brotato_presentation_test)

add_library(brotato_audio STATIC Private/game_audio.cpp)
target_link_libraries(brotato_audio PUBLIC brotato_module audio_module)
add_executable(brotato_game_audio_test Test/game_audio_test.cpp)
target_link_libraries(brotato_game_audio_test PRIVATE brotato_audio)
add_test(NAME brotato_game_audio_test COMMAND brotato_game_audio_test ${CMAKE_SOURCE_DIR}/Asset/Brotato)

add_executable(brotato_game main.cpp)
target_link_libraries(brotato_game PRIVATE brotato_audio render_sprite2d)
if(MINGW)
    target_link_options(brotato_game PRIVATE -municode)
endif()
add_custom_target(brotato_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Asset/Brotato ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Brotato
    COMMENT "Copying imported Brotato assets")
add_dependencies(brotato_game brotato_assets)
