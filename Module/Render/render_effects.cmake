add_library(render_effects STATIC Render/Private/Pipeline/material_render_pipeline.cpp Render/Private/Pipeline/sky_light.cpp
    Render/Private/Pipeline/diffuse_probe_volume.cpp Render/Private/Pipeline/realtime_gi.cpp Render/Private/Pipeline/lumen_gi.cpp)
target_sources(render_effects PRIVATE Render/Private/Backend/Vulkan/backend.cpp)
target_sources(render_effects PRIVATE Render/Private/Pipeline/mesh_distance_field.cpp)
option(RENDER_VULKAN "Build the native Vulkan 1.3 RHI backend" ON)
set(RENDER_VULKAN_AVAILABLE FALSE)
if(RENDER_VULKAN)
    find_package(Vulkan QUIET)
    find_library(RENDER_SHADERC_LIBRARY NAMES shaderc_shared HINTS "$ENV{VULKAN_SDK}/Lib")
endif()
if(RENDER_VULKAN AND Vulkan_FOUND AND RENDER_SHADERC_LIBRARY)
    set(RENDER_VULKAN_AVAILABLE TRUE)
    target_compile_definitions(render_effects PRIVATE RENDER_HAS_VULKAN=1)
    target_link_libraries(render_effects PUBLIC Vulkan::Vulkan "${RENDER_SHADERC_LIBRARY}")
    find_file(RENDER_SHADERC_DLL shaderc_shared.dll HINTS "$ENV{VULKAN_SDK}/Bin")
    if(WIN32 AND RENDER_SHADERC_DLL)
        # Import library is runtime-independent; the compiler DLL carries its CRT.
        add_custom_command(TARGET render_effects POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDER_SHADERC_DLL}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaderc_shared.dll")
    endif()
endif()
target_compile_features(render_effects PUBLIC cxx_std_20)
target_include_directories(render_effects PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Engine/include
    ${CMAKE_SOURCE_DIR}/Engine/include/engine
    ${CMAKE_SOURCE_DIR}/Engine/include/engine/ToolAndAlgorithm
    ${CMAKE_SOURCE_DIR}/Module
    ${CMAKE_SOURCE_DIR}/Module/Render
    ${CMAKE_SOURCE_DIR}/Module/Render/Public
    ${CMAKE_SOURCE_DIR}/Module/Render/Private)
target_link_libraries(render_effects PUBLIC opengl32 glfw3 glad)
set_target_properties(render_effects PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Render)

add_executable(render_target_test Render/Test/render_target_test.cpp)
target_link_libraries(render_target_test PRIVATE render_effects)
add_test(NAME render_target_test COMMAND render_target_test)

add_executable(scene_effects_test Render/Test/scene_effects_test.cpp)
target_include_directories(scene_effects_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME scene_effects_test COMMAND scene_effects_test)

add_executable(post_process_test Render/Test/post_process_test.cpp)
target_include_directories(post_process_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME post_process_test COMMAND post_process_test)

add_executable(material_pipeline_lifecycle_test Render/Test/material_pipeline_lifecycle_test.cpp)
target_link_libraries(material_pipeline_lifecycle_test PRIVATE render_effects)
add_test(NAME material_pipeline_lifecycle_test COMMAND material_pipeline_lifecycle_test)
set_tests_properties(material_pipeline_lifecycle_test PROPERTIES TIMEOUT 10)

add_executable(msaa_render_target_test Render/Test/msaa_render_target_test.cpp)
target_link_libraries(msaa_render_target_test PRIVATE render_effects)
add_test(NAME msaa_render_target_test COMMAND msaa_render_target_test)

add_executable(temporal_effects_test Render/Test/temporal_effects_test.cpp)
target_include_directories(temporal_effects_test PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME temporal_effects_test COMMAND temporal_effects_test)

add_executable(bloom_stability_test Render/Test/bloom_stability_test.cpp)
target_link_libraries(bloom_stability_test PRIVATE render_effects)

add_executable(temporal_effects_gpu_test Render/Test/temporal_effects_gpu_test.cpp)
target_link_libraries(temporal_effects_gpu_test PRIVATE render_effects)

add_executable(material_pipeline_history_test Render/Test/material_pipeline_history_test.cpp)
target_link_libraries(material_pipeline_history_test PRIVATE render_effects)

add_executable(shadow_quality_gpu_test Render/Test/shadow_quality_gpu_test.cpp)
target_link_libraries(shadow_quality_gpu_test PRIVATE render_effects)

add_executable(ssao_quality_gpu_test Render/Test/ssao_quality_gpu_test.cpp)
target_link_libraries(ssao_quality_gpu_test PRIVATE render_effects)

add_executable(pbr_specular_aa_gpu_test Render/Test/pbr_specular_aa_gpu_test.cpp)
target_link_libraries(pbr_specular_aa_gpu_test PRIVATE render_effects)

add_executable(area_light_gpu_test Render/Test/area_light_gpu_test.cpp)
target_link_libraries(area_light_gpu_test PRIVATE render_effects)
add_executable(indirect_lighting_gpu_test Render/Test/indirect_lighting_gpu_test.cpp)
target_link_libraries(indirect_lighting_gpu_test PRIVATE render_effects)
add_executable(advanced_material_test Render/Test/advanced_material_test.cpp)
target_link_libraries(advanced_material_test PRIVATE render_effects)
add_test(NAME advanced_material_test COMMAND advanced_material_test)

add_executable(sky_light_test Render/Test/sky_light_test.cpp)
target_link_libraries(sky_light_test PRIVATE render_effects)
add_test(NAME sky_light_test COMMAND sky_light_test)
add_executable(diffuse_probe_test Render/Test/diffuse_probe_test.cpp)
target_link_libraries(diffuse_probe_test PRIVATE render_effects)
add_test(NAME diffuse_probe_test COMMAND diffuse_probe_test)
add_executable(lumen_gi_test Render/Test/lumen_gi_test.cpp)
target_link_libraries(lumen_gi_test PRIVATE render_effects)
add_test(NAME lumen_gi_test COMMAND lumen_gi_test)
add_executable(vulkan_backend_gpu_test Render/Test/vulkan_backend_gpu_test.cpp)
target_link_libraries(vulkan_backend_gpu_test PRIVATE render_effects)
add_executable(distance_field_test Render/Test/distance_field_test.cpp)
target_link_libraries(distance_field_test PRIVATE render_effects)
add_test(NAME distance_field_test COMMAND distance_field_test)
add_executable(lumen_reflection_gpu_test Render/Test/lumen_reflection_gpu_test.cpp)
target_link_libraries(lumen_reflection_gpu_test PRIVATE render_effects)

# Keep CPU-only CTest usable on machines without an OpenGL display. GPU test
# executables remain available for direct invocation in the normal build.
option(RENDER_EFFECTS_GPU_TESTS "Register OpenGL rendering quality regression tests" OFF)
if(RENDER_EFFECTS_GPU_TESTS)
    foreach(gpu_test bloom_stability_test temporal_effects_gpu_test material_pipeline_history_test shadow_quality_gpu_test ssao_quality_gpu_test pbr_specular_aa_gpu_test area_light_gpu_test indirect_lighting_gpu_test lumen_reflection_gpu_test)
        if(TARGET ${gpu_test})
            add_test(NAME ${gpu_test} COMMAND ${gpu_test})
            set_tests_properties(${gpu_test} PROPERTIES TIMEOUT 60 LABELS "gpu" RUN_SERIAL TRUE)
        endif()
    endforeach()
endif()

option(RENDER_VULKAN_GPU_TESTS "Register native Vulkan rendering/ray query regressions (SDK validation layer required)" OFF)
if(RENDER_VULKAN_GPU_TESTS AND RENDER_VULKAN_AVAILABLE)
    add_test(NAME vulkan_backend_gpu_test COMMAND vulkan_backend_gpu_test --validation)
    add_test(NAME vulkan_lumen_reflection_gpu_test COMMAND lumen_reflection_gpu_test --vulkan)
    add_test(NAME vulkan_software_reflection_gpu_test COMMAND lumen_reflection_gpu_test --vulkan --software-rays)
    set_tests_properties(vulkan_backend_gpu_test vulkan_lumen_reflection_gpu_test vulkan_software_reflection_gpu_test
        PROPERTIES TIMEOUT 90 LABELS "gpu;vulkan" RUN_SERIAL TRUE)
endif()
