include_guard(GLOBAL)

# Keep the upstream library independent of Model/Assimp and of the engine's UI
# adapter. The RHI adapter only needs the core and GLFW platform backend.
set(IMGUI_ROOT "${CMAKE_SOURCE_DIR}/third_party/imgui-1.91.9b")
add_library(imgui_core STATIC
    "${IMGUI_ROOT}/imgui.cpp"
    "${IMGUI_ROOT}/imgui_draw.cpp"
    "${IMGUI_ROOT}/imgui_tables.cpp"
    "${IMGUI_ROOT}/imgui_widgets.cpp")
target_include_directories(imgui_core PUBLIC "${IMGUI_ROOT}")
add_library(ImGui::Core ALIAS imgui_core)

add_library(imgui_glfw STATIC "${IMGUI_ROOT}/backends/imgui_impl_glfw.cpp")
target_include_directories(imgui_glfw PUBLIC "${IMGUI_ROOT}/backends")
target_compile_definitions(imgui_glfw PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(imgui_glfw PUBLIC imgui_core glfw3)
add_library(ImGui::Glfw ALIAS imgui_glfw)

# Retained for the existing skeletal editor while it migrates independently.
# DebugUI does not link this renderer or issue native OpenGL calls.
add_library(imgui_opengl3 STATIC EXCLUDE_FROM_ALL "${IMGUI_ROOT}/backends/imgui_impl_opengl3.cpp")
target_include_directories(imgui_opengl3 PUBLIC "${IMGUI_ROOT}/backends")
target_link_libraries(imgui_opengl3 PUBLIC imgui_core opengl32)
add_library(ImGui::OpenGL3 ALIAS imgui_opengl3)
