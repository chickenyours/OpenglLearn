# The simulation can be linked and tested without a window or graphics context.
set(FOREST_FIRE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(FOREST_FIRE_ECS_SOURCES
    ${FOREST_FIRE_ROOT}/Engine/tests/job_system.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/job_system_schedule.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/context.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/archtype_description.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/archtype_instance.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/archtype_manager.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/archtype_preload_instance.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/scene.cpp
    ${FOREST_FIRE_ROOT}/Engine/tests/chunk_schedule.cpp
)

add_library(forest_fire_module STATIC
    Private/forest_module.cpp
    ${FOREST_FIRE_ECS_SOURCES}
)
target_compile_features(forest_fire_module PUBLIC cxx_std_20)
target_include_directories(forest_fire_module PUBLIC
    ${FOREST_FIRE_ROOT}/include
    ${FOREST_FIRE_ROOT}/Engine/include
    ${FOREST_FIRE_ROOT}/Engine/include/engine
    ${FOREST_FIRE_ROOT}/Engine/include/engine/ToolAndAlgorithm
    ${FOREST_FIRE_ROOT}/Module
)
set_target_properties(forest_fire_module PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(forest_fire_module PUBLIC Threads::Threads)
endif()

add_executable(forest_fire_test Test/forest_fire_test.cpp)
target_link_libraries(forest_fire_test PRIVATE forest_fire_module)
add_test(NAME forest_fire_test COMMAND forest_fire_test)

add_library(forest_fire_render STATIC Private/forest_renderer.cpp)
target_link_libraries(forest_fire_render PUBLIC forest_fire_module)
target_include_directories(forest_fire_render PUBLIC
    ${FOREST_FIRE_ROOT}/Module/ApplicationWindow
    ${FOREST_FIRE_ROOT}/Module/ApplicationWindow/Public
    ${FOREST_FIRE_ROOT}/Module/ApplicationWindow/Private
    ${FOREST_FIRE_ROOT}/Module/Render
    ${FOREST_FIRE_ROOT}/Module/Render/Public
    ${FOREST_FIRE_ROOT}/Module/Render/Private
)
set_target_properties(forest_fire_render PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(forest_fire_render PUBLIC opengl32 glfw3 glad)

add_executable(forest_fire_demo Test/main.cpp)
target_link_libraries(forest_fire_demo PRIVATE forest_fire_render)
