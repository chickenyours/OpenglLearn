# ResourceManager 模块 CMakeLists.txt

set(RESOURCE_MANAGER_MODULE_PUBLIC_HDR
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_manager_module.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_loadable.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_handle.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_load_options.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_control_block.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_manager_module.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_handle.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_load_options.inl
    ${CMAKE_CURRENT_SOURCE_DIR}/Public/resource_control_block.inl
)

set(RESOURCE_MANAGER_MODULE_PRIVATE_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Private/resource_manager_module.cpp
)

add_library(resource_manager_module STATIC ${RESOURCE_MANAGER_MODULE_PUBLIC_HDR} ${RESOURCE_MANAGER_MODULE_PRIVATE_SRC})

target_include_directories(resource_manager_module PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Public
)

target_link_libraries(resource_manager_module PUBLIC
    module_manager
)

target_compile_options(resource_manager_module PRIVATE
    -Wno-nonportable-include-path
    -Wno-pragma-pack
    -Wno-ignored-pragma-intrinsic
    -Wno-ignored-attributes
)
