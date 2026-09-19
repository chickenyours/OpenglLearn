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
