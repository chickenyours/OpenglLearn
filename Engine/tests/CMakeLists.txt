
# 可执行文件
add_executable(MyApp
    ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/archtype_description.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/archtype_instance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/archtype_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scene.cpp
)

# 链接引擎库
# target_link_libraries(MyApp engine)