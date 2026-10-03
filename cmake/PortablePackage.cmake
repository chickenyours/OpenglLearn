include_guard(GLOBAL)
add_custom_target(package_portable)

# Example:
# opengl_add_portable_package(my_game
#   ASSETS "${CMAKE_SOURCE_DIR}/Asset/MyGame|MyGame"
#   DOCS "${CMAKE_SOURCE_DIR}/Module/MyGame/README.md"
#   COMPANIONS my_editor)
function(opengl_add_portable_package target)
    cmake_parse_arguments(PKG "" "" "ASSETS;DOCS;COMPANIONS" ${ARGN})
    if(PKG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown package arguments: ${PKG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET "${target}" OR NOT target MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR "Invalid portable package target: ${target}")
    endif()
    set(_executables "$<TARGET_FILE:${target}>")
    foreach(_companion IN LISTS PKG_COMPANIONS)
        list(APPEND _executables "$<TARGET_FILE:${_companion}>")
    endforeach()
    set(_manifest "${CMAKE_BINARY_DIR}/package-manifests/$<CONFIG>/${target}.cmake")
    file(GENERATE OUTPUT "${_manifest}" CONTENT
"set(PACKAGE_NAME [==[${target}]==])
set(PACKAGE_CONFIG [==[$<CONFIG>]==])
set(PACKAGE_ARCH [==[${CMAKE_SIZEOF_VOID_P}]==])
set(PACKAGE_EXECUTABLES [==[${_executables}]==])
set(PACKAGE_ASSETS [==[${PKG_ASSETS}]==])
set(PACKAGE_DOCS [==[${PKG_DOCS}]==])
set(PACKAGE_SOURCE_ROOT [==[${CMAKE_SOURCE_DIR}]==])
set(PACKAGE_OUTPUT_ROOT [==[${OPENGLLEARN_OUTPUT_ROOT}/$<CONFIG>/packages]==])
set(PACKAGE_RUNTIME_DIR [==[$<TARGET_FILE_DIR:${target}>]==])
set(PACKAGE_READOBJ [==[${OPENGLLEARN_LLVM_READOBJ}]==])
set(PACKAGE_OBJDUMP [==[${OPENGLLEARN_LLVM_OBJDUMP}]==])
set(PACKAGE_STATIC_CRT [==[${OPENGLLEARN_STATIC_RUNTIME}]==])
")
    add_custom_target(package_${target}
        COMMAND "${CMAKE_COMMAND}" "-DMANIFEST=${_manifest}" -P "${CMAKE_SOURCE_DIR}/cmake/PackagePortable.cmake"
        DEPENDS ${target} ${PKG_COMPANIONS}
        COMMENT "Packaging ${target}: executable, runtime dependencies, assets and notices"
        VERBATIM)
    add_dependencies(package_portable package_${target})
endfunction()
