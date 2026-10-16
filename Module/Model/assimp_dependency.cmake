include_guard(GLOBAL)

# Prefer a package/source build which uses the selected configuration and CRT.
# The repository's historical binary is Debug-only and is never used by Release.
set(MODEL_ASSIMP_SOURCE_DIR "" CACHE PATH "Optional Assimp source checkout")
option(MODEL_ASSIMP_FETCH "Download pinned Assimp 6.0.5 when no package/source is available" OFF)
option(MODEL_ASSIMP_DOUBLE_PRECISION "Use double ai_real when building Assimp from source" ON)
set(MODEL_ASSIMP_AVAILABLE FALSE)
set(MODEL_ASSIMP_RUNTIME_FILE "")
set(MODEL_ASSIMP_INCLUDE_DIRS "")

macro(_model_assimp_properties target)
    set(MODEL_ASSIMP_INCLUDE_DIRS "$<TARGET_PROPERTY:${target},INTERFACE_INCLUDE_DIRECTORIES>")
    get_target_property(_model_assimp_type ${target} TYPE)
    if(WIN32 AND _model_assimp_type STREQUAL "SHARED_LIBRARY")
        set(MODEL_ASSIMP_RUNTIME_FILE "$<TARGET_FILE:${target}>")
    endif()
endmacro()

macro(_model_assimp_alias target)
    get_target_property(_model_assimp_actual ${target} ALIASED_TARGET)
    if(_model_assimp_actual)
        add_library(Model::Assimp ALIAS ${_model_assimp_actual})
    else()
        add_library(Model::Assimp ALIAS ${target})
    endif()
endmacro()

find_package(assimp 5.4 CONFIG QUIET)
if(TARGET assimp::assimp)
    _model_assimp_alias(assimp::assimp)
    _model_assimp_properties(assimp::assimp)
    set(MODEL_ASSIMP_AVAILABLE TRUE)
    return()
elseif(TARGET assimp)
    _model_assimp_alias(assimp)
    _model_assimp_properties(assimp)
    set(MODEL_ASSIMP_AVAILABLE TRUE)
    return()
endif()

function(_model_add_assimp_source source_dir)
    # Assimp 6.0.5's unpatched glTF reader byte-copies FLOAT data into double
    # containers. Fail before building/importing rather than silently cooking
    # corrupted geometry. Explicit source checkouts are only inspected here.
    if(MODEL_ASSIMP_DOUBLE_PRECISION)
        file(READ "${source_dir}/CMakeLists.txt" _model_assimp_project)
        if(_model_assimp_project MATCHES "PROJECT\\(Assimp VERSION 6[.]0[.]5[ \r\n]")
            get_filename_component(_model_assimp_checked_source "${source_dir}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" "-DASSIMP_SOURCE_DIR=${_model_assimp_checked_source}"
                    -DCHECK_ONLY=ON -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../cmake/PatchAssimp605GltfDouble.cmake"
                RESULT_VARIABLE _model_assimp_check_result
                OUTPUT_VARIABLE _model_assimp_check_output
                ERROR_VARIABLE _model_assimp_check_error)
            if(NOT _model_assimp_check_result STREQUAL "0")
                message(FATAL_ERROR "Assimp 6.0.5 double-precision source validation failed:\n${_model_assimp_check_output}${_model_assimp_check_error}")
            endif()
        endif()
    endif()
    set(BUILD_SHARED_LIBS OFF)
    # Standalone clang-cl toolchains may supply SDK libraries explicitly rather
    # than through a VS developer shell. Header/function probes also need them.
    if(MSVC)
        foreach(_model_sdk_lib IN LISTS CMAKE_LIBRARY_PATH)
            list(APPEND CMAKE_REQUIRED_LINK_OPTIONS "/LIBPATH:${_model_sdk_lib}")
        endforeach()
    endif()
    set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
    set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
    set(ASSIMP_DOUBLE_PRECISION ${MODEL_ASSIMP_DOUBLE_PRECISION} CACHE BOOL "" FORCE)
    if(OPENGLLEARN_STATIC_RUNTIME)
        set(USE_STATIC_CRT ON CACHE BOOL "" FORCE)
    else()
        set(USE_STATIC_CRT OFF CACHE BOOL "" FORCE)
    endif()
    add_subdirectory("${source_dir}" "${CMAKE_BINARY_DIR}/third_party/model-assimp" EXCLUDE_FROM_ALL)
    # A static import library has no DLL version resource. Exclude Assimp's
    # Windows DLL resource from this static build (also avoids llvm-rc's ANSI
    # handling of its UTF-8 copyright string); shared packages keep theirs.
    if(WIN32)
        get_target_property(_model_assimp_type assimp TYPE)
        if(_model_assimp_type STREQUAL "STATIC_LIBRARY")
            get_target_property(_model_assimp_sources assimp SOURCES)
            list(FILTER _model_assimp_sources EXCLUDE REGEX "[.]rc$")
            set_property(TARGET assimp PROPERTY SOURCES ${_model_assimp_sources})
        endif()
    endif()
    # Older Assimp releases set this only in their directory, not their public
    # usage requirements. A consumer with the wrong ai_real type has a new ABI.
    if(ASSIMP_DOUBLE_PRECISION)
        target_compile_definitions(assimp PUBLIC ASSIMP_DOUBLE_PRECISION)
    endif()
endfunction()

if(MODEL_ASSIMP_SOURCE_DIR)
    if(NOT EXISTS "${MODEL_ASSIMP_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "MODEL_ASSIMP_SOURCE_DIR does not contain an Assimp source checkout")
    endif()
    _model_add_assimp_source("${MODEL_ASSIMP_SOURCE_DIR}")
elseif(MODEL_ASSIMP_FETCH)
    include(FetchContent)
    set(_model_assimp_patch_command)
    if(MODEL_ASSIMP_DOUBLE_PRECISION)
        # Only the pinned FetchContent checkout is modified automatically.
        # For an explicit 6.0.5 SOURCE_DIR, opt in by running this script with
        # -DASSIMP_SOURCE_DIR=<writable-checkout> before configuring.
        list(APPEND _model_assimp_patch_command PATCH_COMMAND
            "${CMAKE_COMMAND}" "-DASSIMP_SOURCE_DIR=<SOURCE_DIR>"
            -P "${CMAKE_CURRENT_LIST_DIR}/../../cmake/PatchAssimp605GltfDouble.cmake")
    endif()
    FetchContent_Declare(model_assimp_source
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v6.0.5
        GIT_SHALLOW TRUE
        ${_model_assimp_patch_command})
    FetchContent_GetProperties(model_assimp_source)
    if(NOT model_assimp_source_POPULATED)
        FetchContent_Populate(model_assimp_source)
    endif()
    _model_add_assimp_source("${model_assimp_source_SOURCE_DIR}")
endif()

if(TARGET assimp::assimp)
    _model_assimp_alias(assimp::assimp)
    _model_assimp_properties(assimp::assimp)
    set(MODEL_ASSIMP_AVAILABLE TRUE)
elseif(TARGET assimp)
    _model_assimp_alias(assimp)
    _model_assimp_properties(assimp)
    set(MODEL_ASSIMP_AVAILABLE TRUE)
elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT CMAKE_CONFIGURATION_TYPES
       AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT OPENGLLEARN_STATIC_RUNTIME
       AND EXISTS "${CMAKE_SOURCE_DIR}/lib/debug/assimp-vc143-mtd.lib"
       AND EXISTS "${CMAKE_SOURCE_DIR}/bin/assimp-vc143-mtd.dll")
    add_library(model_assimp_binary SHARED IMPORTED GLOBAL)
    set_target_properties(model_assimp_binary PROPERTIES
        IMPORTED_CONFIGURATIONS Debug
        IMPORTED_IMPLIB_DEBUG "${CMAKE_SOURCE_DIR}/lib/debug/assimp-vc143-mtd.lib"
        IMPORTED_LOCATION_DEBUG "${CMAKE_SOURCE_DIR}/bin/assimp-vc143-mtd.dll"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/include")
    add_library(Model::Assimp ALIAS model_assimp_binary)
    set(MODEL_ASSIMP_AVAILABLE TRUE)
    set(MODEL_ASSIMP_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/include")
    set(MODEL_ASSIMP_RUNTIME_FILE "${CMAKE_SOURCE_DIR}/bin/assimp-vc143-mtd.dll")
    message(STATUS "Model: using bundled Assimp 5.4.3 Debug DLL (float source precision)")
else()
    message(WARNING "Model module disabled: no compatible Assimp package/source. Set assimp_DIR, MODEL_ASSIMP_SOURCE_DIR, or MODEL_ASSIMP_FETCH=ON. The bundled DLL is only compatible with Windows x64 Debug /MDd.")
endif()
