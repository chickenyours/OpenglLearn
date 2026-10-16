option(MODEL_BUILD "Build static model loading and PBR rendering" ON)
if(NOT MODEL_BUILD)
    return()
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/assimp_dependency.cmake)
if(NOT MODEL_ASSIMP_AVAILABLE)
    # The dependency resolver reports the compatible package/source options.
    return()
endif()

add_library(model_blend STATIC Private/blend_importer.cpp)
target_compile_features(model_blend PUBLIC cxx_std_20)
target_include_directories(model_blend PUBLIC ${CMAKE_SOURCE_DIR}/Module)
target_compile_definitions(model_blend PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_library(model_importer STATIC Private/static_model_importer.cpp)
set_property(TARGET model_importer PROPERTY NO_SYSTEM_FROM_IMPORTED ON)
target_compile_features(model_importer PUBLIC cxx_std_20)
target_include_directories(model_importer PUBLIC ${CMAKE_SOURCE_DIR}/Module ${CMAKE_SOURCE_DIR}/include)
target_include_directories(model_importer BEFORE PRIVATE ${MODEL_ASSIMP_INCLUDE_DIRS})
target_link_libraries(model_importer PUBLIC model_blend PRIVATE Model::Assimp)

add_library(model_module STATIC Private/model_module.cpp)
target_compile_features(model_module PUBLIC cxx_std_20)
target_link_libraries(model_module PUBLIC model_importer render_effects)

add_executable(static_model_test Test/static_model_test.cpp)
target_link_libraries(static_model_test PRIVATE model_importer)
target_compile_definitions(static_model_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME static_model_test COMMAND static_model_test)
set_tests_properties(static_model_test PROPERTIES TIMEOUT 90)

add_executable(gltf_material_test Test/gltf_material_test.cpp)
target_link_libraries(gltf_material_test PRIVATE model_importer)
target_compile_definitions(gltf_material_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME gltf_material_test COMMAND gltf_material_test)
set_tests_properties(gltf_material_test PROPERTIES TIMEOUT 30)

add_executable(model_lifecycle_test Test/model_lifecycle_test.cpp)
target_link_libraries(model_lifecycle_test PRIVATE model_module)
add_test(NAME model_lifecycle_test COMMAND model_lifecycle_test)
set_tests_properties(model_lifecycle_test PROPERTIES TIMEOUT 10)

add_executable(model_viewer Test/model_viewer.cpp)
target_link_libraries(model_viewer PRIVATE model_module)
target_compile_definitions(model_viewer PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(MODEL_ASSIMP_RUNTIME_FILE)
    foreach(model_executable IN ITEMS static_model_test gltf_material_test model_lifecycle_test model_viewer)
        add_custom_command(TARGET ${model_executable} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_ASSIMP_RUNTIME_FILE}" "$<TARGET_FILE_DIR:${model_executable}>")
    endforeach()
endif()

option(MODEL_GPU_TESTS "Register the real-model Render/PBR smoke test" OFF)
if(MODEL_GPU_TESTS)
    add_test(NAME model_rikka_gpu_test COMMAND model_viewer --smoke-test)
    set_tests_properties(model_rikka_gpu_test PROPERTIES TIMEOUT 90 LABELS "gpu" RUN_SERIAL TRUE)
    add_test(NAME model_rikka_toon_gpu_test COMMAND model_viewer --smoke-test --toon)
    set_tests_properties(model_rikka_toon_gpu_test PROPERTIES TIMEOUT 120 LABELS "gpu" RUN_SERIAL TRUE)
endif()

# Intermediate skeletal assets, animation/IK runtime and the asset preview editor.
include(${CMAKE_CURRENT_SOURCE_DIR}/skeletal_assets.cmake)
