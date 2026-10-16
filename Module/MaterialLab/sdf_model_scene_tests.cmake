# CPU-only importer/SDF extraction verification, independent of the demo UI.
if(TARGET model_importer)
    add_executable(material_lab_sdf_model_scene_test Test/sdf_model_scene_test.cpp Private/sdf_model_scene.cpp)
    target_link_libraries(material_lab_sdf_model_scene_test PRIVATE model_importer render_effects)
    target_compile_definitions(material_lab_sdf_model_scene_test PRIVATE SDF_MODEL_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME material_lab_sdf_model_scene_test COMMAND material_lab_sdf_model_scene_test)
    set_tests_properties(material_lab_sdf_model_scene_test PROPERTIES TIMEOUT 120)
    # Model's dependency variable belongs to its directory scope. Query the
    # imported target directly so a CPU-only helper build also carries its DLL.
    if(WIN32 AND TARGET Model::Assimp)
        get_target_property(SDF_MODEL_ASSIMP_TYPE Model::Assimp TYPE)
    endif()
    if(WIN32 AND SDF_MODEL_ASSIMP_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET material_lab_sdf_model_scene_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Model::Assimp>" "$<TARGET_FILE_DIR:material_lab_sdf_model_scene_test>")
    endif()
endif()
