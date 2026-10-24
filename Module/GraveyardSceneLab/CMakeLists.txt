add_executable(graveyard_scene_lab Private/main.cpp)
target_compile_features(graveyard_scene_lab PRIVATE cxx_std_20)
target_link_libraries(graveyard_scene_lab PRIVATE render_effects imgui_core imgui_glfw imgui_opengl3)
target_compile_definitions(graveyard_scene_lab PRIVATE GRAVEYARD_LAB_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# Keep this interactive study usable in the repository's Debug build. Only this
# target is optimized; debug symbols, the Debug CRT and shared Render stay intact.
if(MSVC)
  target_compile_options(graveyard_scene_lab PRIVATE "$<$<CONFIG:Debug>:/O2>" "$<$<CONFIG:Debug>:/Ob2>" "$<$<CONFIG:Debug>:/RTC->")
else()
  target_compile_options(graveyard_scene_lab PRIVATE "$<$<CONFIG:Debug>:-O2>")
endif()
add_test(NAME graveyard_scene_lab_smoke COMMAND graveyard_scene_lab --smoke-test)
set_tests_properties(graveyard_scene_lab_smoke PROPERTIES TIMEOUT 90 LABELS "gpu" RUN_SERIAL TRUE)
