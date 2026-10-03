cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED MANIFEST OR NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "A generated package MANIFEST is required")
endif()
include("${MANIFEST}")
if(NOT WIN32 OR NOT PACKAGE_ARCH EQUAL 8)
    message(FATAL_ERROR "Portable packages currently require Windows x64")
endif()
if(NOT PACKAGE_CONFIG MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    message(FATAL_ERROR "Do not distribute Debug binaries. Use the windows-release preset")
endif()
if(NOT PACKAGE_NAME MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "Invalid package name")
endif()
if(NOT EXISTS "${PACKAGE_READOBJ}" OR NOT EXISTS "${PACKAGE_OBJDUMP}")
    message(FATAL_ERROR "Packaging requires llvm-readobj and llvm-objdump; set OPENGLLEARN_LLVM_READOBJ/OBJDUMP")
endif()
foreach(executable IN LISTS PACKAGE_EXECUTABLES)
    if(NOT EXISTS "${executable}")
        message(FATAL_ERROR "Build the target first: ${executable}")
    endif()
endforeach()

# Resolve the complete PE import closure. Windows system DLLs and API sets are
# supplied by the OS; release VC runtime DLLs are included when using /MD.
set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${PACKAGE_OBJDUMP}")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${PACKAGE_EXECUTABLES}
    DIRECTORIES "${PACKAGE_RUNTIME_DIR}"
    RESOLVED_DEPENDENCIES_VAR dependencies
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
    POST_INCLUDE_REGEXES "[Mm][Ss][Vv][Cc][Pp][0-9]+.*\\.[Dd][Ll][Ll]$" "[Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee][0-9]+.*\\.[Dd][Ll][Ll]$"
    POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")
if(unresolved OR conflicts_FILENAMES)
    message(FATAL_ERROR "Incomplete runtime closure. Missing: ${unresolved}; ambiguous: ${conflicts_FILENAMES}")
endif()

set(import_report "Configuration: ${PACKAGE_CONFIG}\nArchitecture: Windows x64\nStatic CRT requested: ${PACKAGE_STATIC_CRT}\n\n")
foreach(binary IN LISTS PACKAGE_EXECUTABLES dependencies)
    execute_process(COMMAND "${PACKAGE_READOBJ}" --coff-imports "${binary}"
        RESULT_VARIABLE result OUTPUT_VARIABLE imports ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot inspect ${binary}: ${error}")
    endif()
    string(TOLOWER "${imports}" lowercase_imports)
    if(lowercase_imports MATCHES "(msvcp[0-9]+[a-z0-9_]*d|vcruntime[0-9]+[a-z0-9_]*d|ucrtbased)\\.dll")
        message(FATAL_ERROR "Debug CRT imported by ${binary}; rebuild this dependency for Release")
    endif()
    if(PACKAGE_STATIC_CRT AND lowercase_imports MATCHES "(msvcp[0-9]+[a-z0-9_]*|vcruntime[0-9]+[a-z0-9_]*)\\.dll")
        message(FATAL_ERROR "Static CRT package contains dynamic VC runtime dependency: ${binary}")
    endif()
    get_filename_component(name "${binary}" NAME)
    string(APPEND import_report "${name}\n")
    string(REGEX MATCHALL "Name: [^\r\n]+" names "${imports}")
    foreach(import IN LISTS names)
        string(APPEND import_report "  ${import}\n")
    endforeach()
    string(APPEND import_report "\n")
endforeach()

file(MAKE_DIRECTORY "${PACKAGE_OUTPUT_ROOT}")
file(REAL_PATH "${PACKAGE_OUTPUT_ROOT}" output_root)
set(destination "${output_root}/${PACKAGE_NAME}")
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(stage "${output_root}/.stage-${PACKAGE_NAME}-${nonce}")
file(MAKE_DIRECTORY "${stage}" "${stage}/docs" "${stage}/licenses")
foreach(binary IN LISTS PACKAGE_EXECUTABLES dependencies)
    file(COPY "${binary}" DESTINATION "${stage}")
endforeach()
foreach(asset IN LISTS PACKAGE_ASSETS)
    string(REPLACE "|" ";" pair "${asset}")
    list(LENGTH pair size)
    if(NOT size EQUAL 2)
        message(FATAL_ERROR "ASSETS entries must be source|relative-destination: ${asset}")
    endif()
    list(GET pair 0 source)
    list(GET pair 1 relative)
    if(NOT IS_DIRECTORY "${source}" OR IS_ABSOLUTE "${relative}" OR relative MATCHES "(^|[/\\\\])\\.\\.([/\\\\]|$)")
        message(FATAL_ERROR "Invalid asset directory or destination: ${asset}")
    endif()
    file(COPY "${source}/" DESTINATION "${stage}/${relative}"
        PATTERN "*.editor.bak" EXCLUDE PATTERN "*.editor.tmp" EXCLUDE PATTERN "*.editor.old" EXCLUDE)
endforeach()
foreach(document IN LISTS PACKAGE_DOCS)
    if(NOT EXISTS "${document}")
        message(FATAL_ERROR "Package documentation missing: ${document}")
    endif()
    file(COPY "${document}" DESTINATION "${stage}/docs")
endforeach()
file(COPY "${PACKAGE_SOURCE_ROOT}/cmake/licenses/" DESTINATION "${stage}/licenses")
file(WRITE "${stage}/runtime-dependencies.txt" "${import_report}")
file(WRITE "${stage}/.opengllearn-package" "Generated portable package: ${PACKAGE_NAME}\n")
file(WRITE "${stage}/README.txt"
"${PACKAGE_NAME} - ${PACKAGE_CONFIG}, Windows x64

Extract the entire ZIP to a writable folder and run ${PACKAGE_NAME}.exe.
Keep the supplied asset directories and DLLs beside the executables.
Graphical programs require an OpenGL 4.5 capable GPU and its vendor driver.
This package was built with static CRT: ${PACKAGE_STATIC_CRT}.
No Visual Studio, CMake or source checkout is required to play.

Controls and module notes: docs/
Third-party notices: licenses/
Verified PE imports: runtime-dependencies.txt
File integrity: SHA256SUMS.txt

This directory is regenerated by the package target. Back up user saves and
edited levels before rebuilding a package. Do not play from inside the ZIP.
")
file(GLOB_RECURSE payload LIST_DIRECTORIES false RELATIVE "${stage}" "${stage}/*")
list(SORT payload)
set(sums "")
foreach(path IN LISTS payload)
    file(SHA256 "${stage}/${path}" checksum)
    string(APPEND sums "${checksum}  ${path}\n")
endforeach()
file(WRITE "${stage}/SHA256SUMS.txt" "${sums}")

# Only replace an identified, generated child of the resolved output directory.
# Never recursively remove an arbitrary caller-supplied path.
cmake_path(IS_PREFIX output_root "${destination}" NORMALIZE inside_output)
if(NOT inside_output OR destination STREQUAL output_root)
    message(FATAL_ERROR "Unsafe package destination")
endif()
if(EXISTS "${destination}")
    file(REAL_PATH "${destination}" existing_destination)
    cmake_path(IS_PREFIX output_root "${existing_destination}" NORMALIZE existing_inside)
    if(NOT existing_inside OR NOT existing_destination STREQUAL destination OR NOT EXISTS "${destination}/.opengllearn-package")
        message(FATAL_ERROR "Refusing to replace an unrecognized package directory: ${destination}")
    endif()
    file(REMOVE_RECURSE "${destination}")
endif()
file(RENAME "${stage}" "${destination}")
set(archive "${output_root}/${PACKAGE_NAME}-windows-x64.zip")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}.tmp" --format=zip "${PACKAGE_NAME}"
    WORKING_DIRECTORY "${output_root}" RESULT_VARIABLE archive_result)
if(NOT archive_result EQUAL 0)
    message(FATAL_ERROR "ZIP creation failed: ${archive}")
endif()
file(RENAME "${archive}.tmp" "${archive}")
message(STATUS "Portable directory: ${destination}")
message(STATUS "Portable ZIP: ${archive}")
