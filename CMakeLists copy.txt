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
    code/CodeMomeryInsert/memory_insert.cpp
)

set(DLL_SCRIPT_SRC
    code/Resource/Scripts/XuanZhuan.cpp
    code/Resource/Scripts/XianShi.cpp
)

set(DLL_SCRIPT_PROGRAM script_program.cpp)

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
add_library(script MODULE ${DLL_SCRIPT_PROGRAM} ${DLL_SCRIPT_SRC})
set_target_properties(script PROPERTIES
    OUTPUT_NAME "script"
    PREFIX ""
    SUFFIX ".dll"
)

# 放在 add_library(script ...) 之后
# 1) bat 绝对路径
set(ONLY_DERIVED_BAT "${CMAKE_CURRENT_SOURCE_DIR}/code_analyst/only_derived.bat")
if(NOT EXISTS "${ONLY_DERIVED_BAT}")
  message(FATAL_ERROR "only_derived.bat not found at: ${ONLY_DERIVED_BAT}")
endif()

# 2) 输出目录
set(BLUEPRINT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Scripts/BluePrint")
file(MAKE_DIRECTORY "${BLUEPRINT_DIR}")

set(BLUEPRINT_OUTPUTS)  # 别忘了收集输出

# 3) 为每个脚本源生成对应的 .json
foreach(src IN LISTS DLL_SCRIPT_SRC)
  get_filename_component(src_abs "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  get_filename_component(fname   "${src}" NAME_WE)
  set(out_json "${BLUEPRINT_DIR}/${fname}.json")

  add_custom_command(
    OUTPUT "${out_json}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BLUEPRINT_DIR}"
    # 用 chdir 包住 cmd /c 调用 .bat（你的 CMake 没有 -E cmd）
    COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_SOURCE_DIR}" cmd /c
            "${ONLY_DERIVED_BAT}" "${src_abs}" IScript "${out_json}"
    DEPENDS "${src_abs}" "${ONLY_DERIVED_BAT}"
    VERBATIM
    COMMENT "Generating blueprint: ${out_json}"
  )

  list(APPEND BLUEPRINT_OUTPUTS "${out_json}")
endforeach()

add_custom_target(gen_script_blueprints DEPENDS ${BLUEPRINT_OUTPUTS})
add_dependencies(script gen_script_blueprints)

