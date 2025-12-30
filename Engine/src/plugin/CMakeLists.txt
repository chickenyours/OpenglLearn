
set(SCRIPT_MANAGER_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/script_program.cpp
)

# 添加脚本管理器静态库
add_library(script_program STATIC ${SCRIPT_MANAGER_SRC})
target_compile_options(script_program PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)

target_link_libraries(script_program PRIVATE 

)

# 名称为 script_manager
set_target_properties(script_program PROPERTIES
    OUTPUT_NAME "engine_script_manager"
)