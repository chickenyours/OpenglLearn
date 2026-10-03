# Material data remains usable without an OpenGL context or the resource module.
add_executable(material_data_test Render/Test/material_data_test.cpp)
target_include_directories(material_data_test PRIVATE ${RENDER_TEST_INCLUDES})
add_test(NAME material_data_test COMMAND material_data_test)

add_executable(material_runtime_test Render/Test/material_runtime_test.cpp)
target_include_directories(material_runtime_test PRIVATE ${RENDER_TEST_INCLUDES})
target_link_libraries(material_runtime_test PRIVATE opengl32 glfw3 glad)
add_test(NAME material_runtime_test COMMAND material_runtime_test)

# Run explicitly on machines with OpenGL 4.5. Headless unit-test discovery does
# not launch a graphics context automatically.
add_executable(material_render_smoke Render/Test/material_render_smoke.cpp)
target_include_directories(material_render_smoke PRIVATE ${RENDER_TEST_INCLUDES})
target_link_libraries(material_render_smoke PRIVATE opengl32 glfw3 glad)
