
# 可执行文件
add_executable(MyApp
    ${CMAKE_CURRENT_SOURCE_DIR}/test2.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/archtype_description.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/archtype_instance.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/archtype_manager.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/archtype_preload_instance.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/scene.cpp
)

# 为 MyApp 添加编译宏
target_compile_definitions(MyApp PRIVATE ENGINE_DEVELOP)

# 链接引擎库
target_link_libraries(MyApp jsonloader)