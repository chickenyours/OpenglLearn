add_library(model_skeletal STATIC
    Private/skeletal_importer.cpp
    Private/blend_metadata.cpp
    Private/max_metadata.cpp
    Private/fit_profile.cpp
    Private/garment_recognition.cpp
    Private/skeletal_runtime.cpp
    Private/ik_retarget.cpp
    Private/vmd_importer.cpp
    Private/animation_baker.cpp
    Private/garment_builder.cpp
    Private/cloth_solver.cpp
    Private/cloth_simd.cpp
    Private/cloth_area_simd.cpp
    Private/cloth_distance_simd.cpp
    Private/cloth_preview_worker.cpp
    Private/cloth_convex.cpp
    Private/cloth_convex_support_simd.cpp
    Private/cloth_asset_io.cpp
    Private/hair_asset_builder.cpp
    Private/hair_asset_io.cpp
    Private/hair_body_collision.cpp
    Private/hair_solver.cpp
    Private/hair_surface_contact.cpp
    Private/skeletal_asset_io.cpp)

target_compile_features(model_skeletal PUBLIC cxx_std_20)
target_include_directories(model_skeletal BEFORE PRIVATE ${MODEL_ASSIMP_INCLUDE_DIRS})
set_property(TARGET model_skeletal PROPERTY NO_SYSTEM_FROM_IMPORTED ON)
target_link_libraries(model_skeletal PUBLIC model_importer PRIVATE Model::Assimp jsonloader)

add_executable(garment_recognition_test Test/garment_recognition_test.cpp)
target_link_libraries(garment_recognition_test PRIVATE model_skeletal)
add_test(NAME garment_recognition_test COMMAND garment_recognition_test)
set_tests_properties(garment_recognition_test PROPERTIES TIMEOUT 30)
add_executable(blend_metadata_test Test/blend_metadata_test.cpp)
target_link_libraries(blend_metadata_test PRIVATE model_skeletal)
add_test(NAME blend_metadata_test COMMAND blend_metadata_test)
set_tests_properties(blend_metadata_test PROPERTIES TIMEOUT 30)
add_executable(max_metadata_test Test/max_metadata_test.cpp)
target_link_libraries(max_metadata_test PRIVATE model_skeletal jsonloader)
add_test(NAME max_metadata_test COMMAND max_metadata_test)
set_tests_properties(max_metadata_test PROPERTIES TIMEOUT 30)
add_executable(fit_profile_test Test/fit_profile_test.cpp)
target_link_libraries(fit_profile_test PRIVATE model_skeletal jsonloader)
target_compile_definitions(fit_profile_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME fit_profile_test COMMAND fit_profile_test)
set_tests_properties(fit_profile_test PROPERTIES TIMEOUT 30)
add_executable(blend_asset_test Test/blend_asset_test.cpp)
target_link_libraries(blend_asset_test PRIVATE model_skeletal)
target_compile_definitions(blend_asset_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME blend_asset_test COMMAND blend_asset_test)
set_tests_properties(blend_asset_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
add_executable(blend_pose_reference_test Test/blend_pose_reference_test.cpp)
target_link_libraries(blend_pose_reference_test PRIVATE model_skeletal jsonloader)

add_executable(skeletal_asset_import Tools/skeletal_asset_import.cpp)
target_link_libraries(skeletal_asset_import PRIVATE model_skeletal)

add_executable(skeletal_animation_import Tools/skeletal_animation_import.cpp)
target_link_libraries(skeletal_animation_import PRIVATE model_skeletal)

add_executable(garment_asset_import Tools/garment_asset_import.cpp)
target_link_libraries(garment_asset_import PRIVATE model_skeletal)
add_executable(hair_asset_import Tools/hair_asset_import.cpp)
target_link_libraries(hair_asset_import PRIVATE model_skeletal)
foreach(hair_test IN ITEMS hair_asset_test hair_body_collision_test hair_surface_contact_test hair_solver_test hair_preview_worker_test)
    add_executable(${hair_test} Test/${hair_test}.cpp)
    target_link_libraries(${hair_test} PRIVATE model_skeletal)
    target_compile_definitions(${hair_test} PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${hair_test} COMMAND ${hair_test})
    set_tests_properties(${hair_test} PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
endforeach()
add_executable(hair_surface_quality_test Test/hair_surface_quality_test.cpp)
target_link_libraries(hair_surface_quality_test PRIVATE model_skeletal jsonloader)
add_test(NAME hair_surface_quality_test COMMAND hair_surface_quality_test --frames 553)
set_tests_properties(hair_surface_quality_test PROPERTIES TIMEOUT 600 LABELS "quality;slow" RUN_SERIAL TRUE WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_test(NAME hair_realtime30_quality_test COMMAND hair_surface_quality_test --frames 277
    --hair "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Hair/Hair_Realtime30.hair.json")
set_tests_properties(hair_realtime30_quality_test PROPERTIES TIMEOUT 600 LABELS "quality;slow" RUN_SERIAL TRUE WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
add_executable(hair_rest_stability_test Test/hair_rest_stability_test.cpp)
target_link_libraries(hair_rest_stability_test PRIVATE model_skeletal jsonloader)
target_compile_definitions(hair_rest_stability_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME hair_rest_stability_test COMMAND hair_rest_stability_test)
set_tests_properties(hair_rest_stability_test PROPERTIES TIMEOUT 300 LABELS "quality;slow" RUN_SERIAL TRUE WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
if(MODEL_ASSIMP_RUNTIME_FILE)
    add_custom_command(TARGET hair_rest_stability_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_ASSIMP_RUNTIME_FILE}" "$<TARGET_FILE_DIR:hair_rest_stability_test>")
endif()
foreach(cloth_test IN ITEMS garment_builder_test cloth_solver_test cloth_asset_io_test cloth_attachment_test cloth_convex_test cloth_body_fit_test)
    add_executable(${cloth_test} Test/${cloth_test}.cpp)
    target_link_libraries(${cloth_test} PRIVATE model_skeletal)
    target_compile_definitions(${cloth_test} PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${cloth_test} COMMAND ${cloth_test})
    set_tests_properties(${cloth_test} PROPERTIES TIMEOUT 360)
endforeach()

add_test(NAME cloth_reset_pose_test COMMAND cloth_attachment_test --sweep-resets)
set_tests_properties(cloth_reset_pose_test PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)

add_executable(cloth_parallel_test Test/cloth_parallel_test.cpp)
target_compile_features(cloth_parallel_test PRIVATE cxx_std_20)
target_include_directories(cloth_parallel_test PRIVATE ${CMAKE_SOURCE_DIR}/Module)
foreach(cloth_threads IN ITEMS 1 4 8)
    add_test(NAME cloth_parallel_${cloth_threads}_test COMMAND cloth_parallel_test)
    set_tests_properties(cloth_parallel_${cloth_threads}_test PROPERTIES
        ENVIRONMENT "MODEL_CLOTH_THREADS=${cloth_threads}" TIMEOUT 30 RUN_SERIAL TRUE)
endforeach()

add_executable(cloth_simd_test Test/cloth_simd_test.cpp)
target_link_libraries(cloth_simd_test PRIVATE model_skeletal)
add_test(NAME cloth_simd_test COMMAND cloth_simd_test)
set_tests_properties(cloth_simd_test PROPERTIES TIMEOUT 60)
add_test(NAME cloth_simd_fallback_test COMMAND cloth_simd_test)
set_tests_properties(cloth_simd_fallback_test PROPERTIES TIMEOUT 60 ENVIRONMENT "MODEL_CLOTH_SIMD=0")
add_executable(cloth_preview_worker_test Test/cloth_preview_worker_test.cpp)
target_link_libraries(cloth_preview_worker_test PRIVATE model_skeletal)
add_test(NAME cloth_preview_worker_test COMMAND cloth_preview_worker_test)
set_tests_properties(cloth_preview_worker_test PROPERTIES TIMEOUT 60)
add_executable(cloth_convex_support_simd_test Test/cloth_convex_support_simd_test.cpp)
target_link_libraries(cloth_convex_support_simd_test PRIVATE model_skeletal)
add_test(NAME cloth_convex_support_simd_test COMMAND cloth_convex_support_simd_test)
add_test(NAME cloth_convex_support_simd_fallback_test COMMAND cloth_convex_support_simd_test)
add_test(NAME cloth_convex_support_simd_global_fallback_test COMMAND cloth_convex_support_simd_test)
set_tests_properties(cloth_convex_support_simd_test cloth_convex_support_simd_fallback_test cloth_convex_support_simd_global_fallback_test PROPERTIES TIMEOUT 60)
set_tests_properties(cloth_convex_support_simd_fallback_test PROPERTIES ENVIRONMENT "MODEL_CLOTH_CONVEX_SIMD=0")
set_tests_properties(cloth_convex_support_simd_global_fallback_test PROPERTIES ENVIRONMENT "MODEL_CLOTH_SIMD=0")

add_executable(preview_animation_clock_test Test/preview_animation_clock_test.cpp)
target_compile_features(preview_animation_clock_test PRIVATE cxx_std_20)
target_include_directories(preview_animation_clock_test PRIVATE ${CMAKE_SOURCE_DIR}/Module)
add_test(NAME preview_animation_clock_test COMMAND preview_animation_clock_test)
set_tests_properties(preview_animation_clock_test PROPERTIES TIMEOUT 15)
foreach(vector_kernel IN ITEMS cloth_area_simd cloth_distance_simd)
    add_executable(${vector_kernel}_test Test/${vector_kernel}_test.cpp)
    target_link_libraries(${vector_kernel}_test PRIVATE model_skeletal)
    add_test(NAME ${vector_kernel}_test COMMAND ${vector_kernel}_test)
    add_test(NAME ${vector_kernel}_fallback_test COMMAND ${vector_kernel}_test)
    set_tests_properties(${vector_kernel}_test ${vector_kernel}_fallback_test PROPERTIES TIMEOUT 60)
    set_tests_properties(${vector_kernel}_fallback_test PROPERTIES ENVIRONMENT "MODEL_CLOTH_SIMD=0")
endforeach()

foreach(animation_test IN ITEMS vmd_import_test animation_bake_test animation_asset_io_test)
    add_executable(${animation_test} Test/${animation_test}.cpp)
    target_link_libraries(${animation_test} PRIVATE model_skeletal)
    target_compile_definitions(${animation_test} PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${animation_test} COMMAND ${animation_test})
    set_tests_properties(${animation_test} PROPERTIES TIMEOUT 120)
endforeach()

add_executable(skeletal_asset_io_test Test/skeletal_asset_io_test.cpp)
target_link_libraries(skeletal_asset_io_test PRIVATE model_skeletal)
target_compile_definitions(skeletal_asset_io_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME skeletal_asset_io_test COMMAND skeletal_asset_io_test)
set_tests_properties(skeletal_asset_io_test PROPERTIES TIMEOUT 60)

add_executable(skeletal_runtime_test Test/skeletal_runtime_test.cpp)
target_link_libraries(skeletal_runtime_test PRIVATE model_skeletal)
target_compile_definitions(skeletal_runtime_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME skeletal_runtime_test COMMAND skeletal_runtime_test)
set_tests_properties(skeletal_runtime_test PROPERTIES TIMEOUT 60)

add_executable(skeletal_import_test Test/skeletal_import_test.cpp)
target_link_libraries(skeletal_import_test PRIVATE model_skeletal)
target_compile_definitions(skeletal_import_test PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME skeletal_import_test COMMAND skeletal_import_test)
set_tests_properties(skeletal_import_test PROPERTIES TIMEOUT 120)

add_library(model_editor_imgui INTERFACE)
target_link_libraries(model_editor_imgui INTERFACE imgui_core imgui_glfw imgui_opengl3)

add_executable(skeletal_asset_editor Tools/skeletal_asset_editor.cpp)
target_link_libraries(skeletal_asset_editor PRIVATE model_skeletal model_module model_editor_imgui)
if(TARGET hair_simulation)
    target_link_libraries(skeletal_asset_editor PRIVATE hair_simulation)
endif()
if(WIN32)
    target_link_libraries(skeletal_asset_editor PRIVATE comdlg32)
endif()
target_compile_definitions(skeletal_asset_editor PRIVATE MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

option(MODEL_OPTIMIZE_PREVIEW "Optimize model preview in Clang Debug builds while retaining debug ABI and symbols" ON)
if(MODEL_OPTIMIZE_PREVIEW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND MSVC)
    # clang-cl keeps /Ob0 as -fno-inline even when -O2 is appended. Explicitly
    # restore inlining for GLM and hot loops; retain /MDd, iterator checks and /Zi
    # so every dependency still shares the same Debug ABI. Disable this option
    # when instruction-by-instruction debugging of these targets is needed.
    foreach(preview_target IN ITEMS model_importer model_skeletal model_module skeletal_asset_editor imgui_core imgui_glfw imgui_opengl3)
        target_compile_options(${preview_target} PRIVATE
            "$<$<CONFIG:Debug>:/clang:-O2>"
            "$<$<CONFIG:Debug>:/clang:-finline>"
            "$<$<CONFIG:Debug>:/clang:-finline-functions>")
    endforeach()
endif()

if(MODEL_ASSIMP_RUNTIME_FILE)
    foreach(skeletal_executable IN ITEMS skeletal_asset_import skeletal_animation_import skeletal_asset_editor garment_asset_import hair_asset_import hair_asset_test hair_body_collision_test hair_surface_contact_test hair_solver_test hair_preview_worker_test hair_surface_quality_test garment_builder_test garment_recognition_test blend_metadata_test max_metadata_test fit_profile_test blend_asset_test blend_pose_reference_test cloth_solver_test cloth_asset_io_test skeletal_asset_io_test skeletal_runtime_test skeletal_import_test vmd_import_test animation_bake_test animation_asset_io_test)
        add_custom_command(TARGET ${skeletal_executable} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_ASSIMP_RUNTIME_FILE}" "$<TARGET_FILE_DIR:${skeletal_executable}>")
    endforeach()
endif()
if(MODEL_GPU_TESTS)
    if(TARGET hair_simulation_gpu_test)
        add_test(NAME diana_character_gpu_test COMMAND skeletal_asset_editor --diana-test --frames 7)
        set_tests_properties(diana_character_gpu_test PROPERTIES TIMEOUT 180 LABELS "gpu;character" RUN_SERIAL TRUE)
    endif()
    if(TARGET hair_simulation_gpu_test)
        foreach(gravity_mode IN ITEMS normal low zero)
            add_test(NAME model_strand_${gravity_mode}_gpu_test COMMAND skeletal_asset_editor
                --strand-test --gravity ${gravity_mode} --frames 90)
            set_tests_properties(model_strand_${gravity_mode}_gpu_test PROPERTIES
                TIMEOUT 180 LABELS "gpu;hair" RUN_SERIAL TRUE)
        endforeach()
    endif()
    add_test(NAME skeletal_editor_gpu_test COMMAND skeletal_asset_editor --smoke-test
        --source "${CMAKE_SOURCE_DIR}/Asset/Model/Rikka_Uniform/Rikka_Uniform.pmx")
    set_tests_properties(skeletal_editor_gpu_test PROPERTIES TIMEOUT 120 LABELS "gpu" RUN_SERIAL TRUE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/Asset/Generated/Animations/MahiroTaitui.anim.json" AND EXISTS "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroMaid/Package.asset.json")
        add_test(NAME vmd_animation_gpu_test COMMAND skeletal_asset_editor --motion-test
            --package "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroMaid/Package.asset.json"
            --animation "${CMAKE_SOURCE_DIR}/Asset/Generated/Animations/MahiroTaitui.anim.json"
            --curve-bone "左足" --time 1)
        set_tests_properties(vmd_animation_gpu_test PROPERTIES TIMEOUT 120 LABELS "gpu" RUN_SERIAL TRUE)
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Character.asset.json")
        add_test(NAME cloth_editor_gpu_test COMMAND skeletal_asset_editor --cloth-test
            --character "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Character.asset.json"
            --animation "${CMAKE_SOURCE_DIR}/Asset/Generated/Animations/MahiroTaitui.anim.json")
        set_tests_properties(cloth_editor_gpu_test PROPERTIES TIMEOUT 300 LABELS "gpu" RUN_SERIAL TRUE)
        add_test(NAME cloth_editor_hold_gpu_test COMMAND skeletal_asset_editor --cloth-test --cloth-hold-at 1 --frames 180
            --character "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Character.asset.json")
        add_test(NAME cloth_editor_equip_gpu_test COMMAND skeletal_asset_editor --cloth-equip-test --frames 90
            --character "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Character.asset.json")
        add_test(NAME cloth_async_editor_gpu_test COMMAND skeletal_asset_editor --async-cloth-test
            --benchmark-frames 360 --benchmark-warmup 60 --cloth-hold-at 1
            --character "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Character.asset.json")
        set_tests_properties(cloth_editor_hold_gpu_test cloth_editor_equip_gpu_test PROPERTIES TIMEOUT 300 LABELS "gpu" RUN_SERIAL TRUE)
        set_tests_properties(cloth_async_editor_gpu_test PROPERTIES TIMEOUT 300 LABELS "gpu" RUN_SERIAL TRUE)
        add_test(NAME hair_editor_gpu_test COMMAND skeletal_asset_editor --hair-test --no-cloth --frames 90
            --character "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Character.asset.json"
            --hair "${CMAKE_SOURCE_DIR}/Asset/Generated/MahiroCloth/Hair/Hair.hair.json")
        set_tests_properties(hair_editor_gpu_test PROPERTIES TIMEOUT 120 LABELS "gpu" RUN_SERIAL TRUE)
    endif()
endif()
