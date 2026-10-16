add_library(debug_ui_module STATIC
    Private/debug_ui_module.cpp
    Private/debug_ui_draw_data.cpp)
target_compile_features(debug_ui_module PUBLIC cxx_std_20)
target_include_directories(debug_ui_module PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module)
target_link_libraries(debug_ui_module PUBLIC imgui_core imgui_glfw render_effects)

# This executable only links the CPU conversion object and upstream ImGui core;
# it does not create a window, device, or GPU context.
add_executable(debug_ui_draw_data_test Test/draw_data_test.cpp)
target_link_libraries(debug_ui_draw_data_test PRIVATE debug_ui_module)
add_test(NAME debug_ui_draw_data_test COMMAND debug_ui_draw_data_test)
