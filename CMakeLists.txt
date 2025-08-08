cmake_minimum_required(VERSION 3.20)
project(MyProject LANGUAGES CXX)

# 编译属性
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置 Debug 构建类型和使用 /MDd
set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
foreach(flag_var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
    string(REPLACE "/MD" "/MDd" ${flag_var} "${${flag_var}}")
endforeach()

# 设置项目include路径
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/code
)

# 设置项目链接库路径
link_directories(
    ${CMAKE_SOURCE_DIR}/lib/debug
)

# 设置输出路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

# 定义主程序源文件和脚本 DLL 文件
set(SRC
    include/stb_image.cpp
    code/Resource/Texture/texture.cpp
    code/Resource/Shader/shader.cpp
    code/Resource/Shader/shader_factory.cpp
    code/Resource/Shader/shader_manager.cpp
    code/Resource/Shader/shader_program.cpp
    code/Resource/Shader/shader_program_factory.cpp
    code/Resource/Model/mesh.cpp
    code/Resource/Model/model.cpp
    code/ToolAndAlgorithm/Hash/md5.cpp
    code/Resource/Material/material.cpp
    code/Application/application.cpp
    code/Scene/scene.cpp
    code/ECS/System/SceneTree/scene_tree.cpp
    code/ECS/System/Script/script_system.cpp
)

set(DLL_SCRIPT_SRC
    script_program.cpp
    code/Resource/Scripts/xuanzhuan.cpp
)

# 传入 TARGET 源文件变量，默认为 main.cpp
set(TARGET apprun.cpp)


# 添加主程序可执行文件
add_executable(main_exec ${SRC} ${TARGET})

# 主程序编译依赖项目编译指令
target_compile_options(main_exec PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# 连接项目库
target_link_libraries(main_exec PRIVATE 
    assimp-vc143-mtd.lib
    glad.lib
    jsonloader.lib
    glfw3.lib
)

# 添加 DLL 插件
add_library(script MODULE ${DLL_SCRIPT_SRC})
set_target_properties(script PROPERTIES
    OUTPUT_NAME "script"
    PREFIX ""
    SUFFIX ".dll"
)
