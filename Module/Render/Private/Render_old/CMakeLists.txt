add_executable(rhi_buffer_test
    ${CMAKE_CURRENT_SOURCE_DIR}/Render/Test/test1.cpp
)

target_include_directories(rhi_buffer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/Render
    ${CMAKE_SOURCE_DIR}/Module/Render/Public
    ${CMAKE_SOURCE_DIR}/Module/Render/Private
)

target_link_directories(rhi_buffer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/debug
)

target_link_libraries(rhi_buffer_test PRIVATE
    opengl32
    glfw3
)