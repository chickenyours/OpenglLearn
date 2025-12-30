# 引擎构建项目


# 定义引擎源文件和脚本 DLL 文件
set(ENGINE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/StdImage/stb_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Texture/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Shader/shader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Shader/shader_factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Shader/shader_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Shader/shader_program.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Shader/shader_program_factory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Model/mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Model/model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ToolAndAlgorithm/Hash/md5.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Resource/Material/material.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Scene/scene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ECS/System/SceneTree/scene_tree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeMomeryInsert/memory_insert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ECS/System/Script/script_system.cpp
)

# 生成静态库 .lib
add_library(engine STATIC ${ENGINE_SRC})


