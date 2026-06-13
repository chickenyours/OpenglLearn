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