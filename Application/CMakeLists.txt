# 这是主程序构建文件

# 获取引擎项目根目录
set(ENGINE_ROOT_DIR ${CMAKE_SOURCE_DIR}/Engine)
set(ASSET_ROOT_DIR ${CMAKE_SOURCE_DIR}/Asset)

# 定义主程序源文件
set(APPLICATION_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/application.cpp
)

# 定义脚本 DLL 源文件
set(DLL_SCRIPT_SRC
    ${ASSET_ROOT_DIR}/Scripts/XuanZhuan.cpp
    ${ASSET_ROOT_DIR}/Scripts/XianShi.cpp
)

# 传入 TARGET 源文件变量，默认为 main.cpp
set(TARGET ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)


# 添加主程序可执行文件
add_executable(main_exec ${APPLICATION_SRC} ${TARGET})

# 主程序编译依赖项目编译指令
target_compile_options(main_exec PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

# 连接项目库
target_link_libraries(main_exec PRIVATE 
    assimp-vc143-mtd
    glad
    jsonloader
    glfw3
    engine
)

# 添加 DLL 插件
add_library(script MODULE ${DLL_SCRIPT_SRC})
set_target_properties(script PROPERTIES
    OUTPUT_NAME "script"
    PREFIX ""
    SUFFIX ".dll"
)

target_link_libraries(script PRIVATE 
    engine_script_manager
)

# 放在 add_library(script ...) 之后
# 1) bat 绝对路径
set(ONLY_DERIVED_BAT ${ENGINE_ROOT_DIR}/tools/code_analyst/only_derived.bat)
if(NOT EXISTS ${ONLY_DERIVED_BAT})
  message(FATAL_ERROR "only_derived.bat not found at: ${ONLY_DERIVED_BAT}")
endif()

# 2) 输出目录
set(BLUEPRINT_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Scripts/BluePrint)
file(MAKE_DIRECTORY ${BLUEPRINT_DIR})

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

