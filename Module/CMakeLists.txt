add_executable(test1
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/test2.cpp
)

target_include_directories(test1 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm/
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/Public
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/Private
    ${CMAKE_SOURCE_DIR}/Module/Render
    ${CMAKE_SOURCE_DIR}/Module/Render/Public
    ${CMAKE_SOURCE_DIR}/Module/Render/Private
)

target_link_directories(test1 PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/debug
)

target_link_libraries(test1 PRIVATE
    opengl32
    glfw3
    glad
)

add_executable(rhi_frame_command_buffer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/frame_command_buffer_test.cpp
)

target_include_directories(rhi_frame_command_buffer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/Render
    ${CMAKE_SOURCE_DIR}/Module/Render/Public
)

add_executable(terrain_pipeline_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_pipeline_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp
)
target_include_directories(terrain_pipeline_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)
if(NOT WIN32)
    target_link_libraries(terrain_pipeline_test PRIVATE pthread)
endif()

# >>> dynamic_terrain_clean_patch_v1 >>>
add_executable(terrain_render_demo
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_render_demo.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp
)

target_include_directories(terrain_render_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/Public
    ${CMAKE_SOURCE_DIR}/Module/ApplicationWindow/Private
    ${CMAKE_SOURCE_DIR}/Module/Render
    ${CMAKE_SOURCE_DIR}/Module/Render/Public
    ${CMAKE_SOURCE_DIR}/Module/Render/Private
)

target_link_directories(terrain_render_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/debug
)

target_link_libraries(terrain_render_demo PRIVATE
    opengl32
    glfw3
    glad
)
if(NOT WIN32)
    target_link_libraries(terrain_render_demo PRIVATE pthread)
endif()
# <<< dynamic_terrain_clean_patch_v1 <<<

add_executable(render_scheduling_test Render/Test/render_scheduling_test.cpp)
get_target_property(RENDER_TEST_INCLUDES terrain_render_demo INCLUDE_DIRECTORIES)
target_include_directories(render_scheduling_test PRIVATE ${RENDER_TEST_INCLUDES})
target_link_directories(render_scheduling_test PRIVATE ${CMAKE_SOURCE_DIR}/lib/debug)
target_link_libraries(render_scheduling_test PRIVATE opengl32 glfw3 glad)

# Task 0.1: headless ECS / Terrain performance baseline.
add_executable(terrain_benchmark
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_benchmark.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp
)
target_include_directories(terrain_benchmark PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)
target_compile_definitions(terrain_benchmark PRIVATE ECS_PROFILING_ENABLED=1)
if(NOT WIN32)
    target_link_libraries(terrain_benchmark PRIVATE pthread)
endif()

# Task 1.3: worker-count policy verification.
add_executable(job_system_worker_config_test
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_worker_config_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
)
target_include_directories(job_system_worker_config_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)
if(NOT WIN32)
    target_link_libraries(job_system_worker_config_test PRIVATE pthread)
endif()

# Task Terrain-1: density field determinism / boundary / throughput test.
add_executable(terrain_density_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_density_test.cpp
)
target_include_directories(terrain_density_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Task Terrain-2: noise router terrain test.
add_executable(terrain_noise_router_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_noise_router_test.cpp
)
target_include_directories(terrain_noise_router_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Task Terrain-3: region + biome + surface rules test.
add_executable(terrain_biome_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_biome_test.cpp
)
target_include_directories(terrain_biome_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Task Terrain-5: continental-scale terrain test.
add_executable(terrain_large_scale_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_large_scale_test.cpp
)
target_include_directories(terrain_large_scale_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Phase 1: block material atlas test.
add_executable(terrain_block_atlas_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_block_atlas_test.cpp
)
target_include_directories(terrain_block_atlas_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Cave generation: determinism, border consistency, surface protection.
add_executable(terrain_cave_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_cave_test.cpp
)
target_include_directories(terrain_cave_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Voxel raycast (3D DDA) test.
add_executable(terrain_raycast_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_raycast_test.cpp
)
target_include_directories(terrain_raycast_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)

# Async stale-mesh / revision token test.
add_executable(terrain_async_edit_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_async_edit_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp
)
target_include_directories(terrain_async_edit_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)
if(NOT WIN32)
    target_link_libraries(terrain_async_edit_test PRIVATE pthread)
endif()

# Block edit overlay / boundary remesh test.
add_executable(terrain_interaction_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Terrain/Test/terrain_interaction_test.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/job_system_schedule.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/context.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_description.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_manager.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/archtype_preload_instance.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/scene.cpp
    ${CMAKE_SOURCE_DIR}/Engine/tests/chunk_schedule.cpp
)
target_include_directories(terrain_interaction_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
)
if(NOT WIN32)
    target_link_libraries(terrain_interaction_test PRIVATE pthread)
endif()







