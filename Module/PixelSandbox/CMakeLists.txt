cmake_minimum_required(VERSION 3.20)
get_filename_component(PIXEL_SANDBOX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PixelSandboxHeadless LANGUAGES CXX)
    enable_testing()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set(PIXEL_SANDBOX_STANDALONE ON)
endif()
set(PIXEL_SANDBOX_ECS_SOURCES
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/job_system.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/job_system_schedule.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/context.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/archtype_description.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/archtype_instance.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/archtype_manager.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/archtype_preload_instance.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/scene.cpp
    ${PIXEL_SANDBOX_ROOT}/Engine/tests/chunk_schedule.cpp)
add_library(pixel_sandbox_module STATIC Private/sandbox_module.cpp Private/materials.cpp ${PIXEL_SANDBOX_ECS_SOURCES})
target_compile_features(pixel_sandbox_module PUBLIC cxx_std_20)
target_include_directories(pixel_sandbox_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
    ${PIXEL_SANDBOX_ROOT}/include ${PIXEL_SANDBOX_ROOT}/Engine/include
    ${PIXEL_SANDBOX_ROOT}/Engine/include/engine ${PIXEL_SANDBOX_ROOT}/Engine/include/engine/ToolAndAlgorithm
    ${PIXEL_SANDBOX_ROOT}/Module)
set_target_properties(pixel_sandbox_module PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(pixel_sandbox_module PUBLIC Threads::Threads)
endif()

add_executable(pixel_sandbox_test Test/sandbox_test.cpp)
target_link_libraries(pixel_sandbox_test PRIVATE pixel_sandbox_module)
add_test(NAME pixel_sandbox_test COMMAND pixel_sandbox_test)
set_tests_properties(pixel_sandbox_test PROPERTIES TIMEOUT 60)
add_executable(pixel_sandbox_benchmark Test/sandbox_benchmark.cpp)
target_link_libraries(pixel_sandbox_benchmark PRIVATE pixel_sandbox_module)

# Rules call a deliberately narrow local API. Link-time inlining eliminates the
# abstraction cost in optimized standalone builds without changing Debug usage.
if(PIXEL_SANDBOX_STANDALONE)
    option(PIXEL_SANDBOX_IPO "Enable supported interprocedural optimization" ON)
    if(PIXEL_SANDBOX_IPO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT PIXEL_SANDBOX_IPO_SUPPORTED OUTPUT PIXEL_SANDBOX_IPO_ERROR LANGUAGES CXX)
        if(PIXEL_SANDBOX_IPO_SUPPORTED)
            set_property(TARGET pixel_sandbox_module pixel_sandbox_test pixel_sandbox_benchmark
                PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        else()
            message(STATUS "PixelSandbox IPO unavailable: ${PIXEL_SANDBOX_IPO_ERROR}")
        endif()
    endif()
endif()

if(NOT PIXEL_SANDBOX_STANDALONE)
add_library(pixel_sandbox_render STATIC Private/sandbox_renderer.cpp)
target_link_libraries(pixel_sandbox_render PUBLIC pixel_sandbox_module render_sprite2d)
set_target_properties(pixel_sandbox_render PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_executable(pixel_sandbox_demo Test/main.cpp)
target_link_libraries(pixel_sandbox_demo PRIVATE pixel_sandbox_render)
add_executable(pixel_sandbox_ui_test Test/sandbox_ui_test.cpp)
target_link_libraries(pixel_sandbox_ui_test PRIVATE pixel_sandbox_render)
add_test(NAME pixel_sandbox_ui_test COMMAND pixel_sandbox_ui_test)

option(PIXEL_SANDBOX_GPU_TESTS "Register the PixelSandbox hidden-window GPU test" OFF)
if(PIXEL_SANDBOX_GPU_TESTS)
    add_test(NAME pixel_sandbox_gpu_test COMMAND pixel_sandbox_demo --smoke-test)
    set_tests_properties(pixel_sandbox_gpu_test PROPERTIES TIMEOUT 90 LABELS gpu)
endif()

endif()
