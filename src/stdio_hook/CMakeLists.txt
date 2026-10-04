cmake_minimum_required(VERSION 3.28)

project(fp_stdio_hook LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "fp_stdio_hook requires Windows")
endif()

if(NOT FASTPIPE_SOURCE_DIR)
    get_filename_component(FASTPIPE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(DETOURS_SOURCE_DIR "${FASTPIPE_SOURCE_DIR}/third_party/Detours/src")
string(TOLOWER "${FASTPIPE_INJECTION_ARCH}" FASTPIPE_INJECTION_ARCH_LOWER)

set(DETOURS_SOURCES
    "${DETOURS_SOURCE_DIR}/creatwth.cpp"
    "${DETOURS_SOURCE_DIR}/detours.cpp"
    "${DETOURS_SOURCE_DIR}/disasm.cpp"
    "${DETOURS_SOURCE_DIR}/disolarm.cpp"
    "${DETOURS_SOURCE_DIR}/disolarm64.cpp"
    "${DETOURS_SOURCE_DIR}/disolia64.cpp"
    "${DETOURS_SOURCE_DIR}/disolx64.cpp"
    "${DETOURS_SOURCE_DIR}/disolx86.cpp"
    "${DETOURS_SOURCE_DIR}/image.cpp"
    "${DETOURS_SOURCE_DIR}/modules.cpp"
)

add_library(detours STATIC ${DETOURS_SOURCES})
target_include_directories(detours PUBLIC "${DETOURS_SOURCE_DIR}")
target_compile_definitions(detours PRIVATE
    DETOUR_DEBUG=0
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0501
)
set_target_properties(detours PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

if(MSVC)
    target_compile_options(detours PRIVATE /W3 /EHsc)
endif()

add_library(fp_stdio_hook SHARED hook.cpp)
target_include_directories(fp_stdio_hook PRIVATE "${DETOURS_SOURCE_DIR}")
target_compile_definitions(fp_stdio_hook PRIVATE
    FASTPIPE_STDIO_HOOK_EXPORTS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)
target_link_libraries(fp_stdio_hook PRIVATE detours ntdll)
set_target_properties(fp_stdio_hook PROPERTIES
    OUTPUT_NAME "fp_stdio_hook_${FASTPIPE_INJECTION_ARCH_LOWER}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

if(MSVC)
    target_compile_options(fp_stdio_hook PRIVATE /W4 /EHsc /permissive-)
endif()

if(FASTPIPE_OUTPUT_DIR)
    set_target_properties(fp_stdio_hook PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${FASTPIPE_OUTPUT_DIR}/$<CONFIG>"
        LIBRARY_OUTPUT_DIRECTORY "${FASTPIPE_OUTPUT_DIR}/$<CONFIG>"
        ARCHIVE_OUTPUT_DIRECTORY "${FASTPIPE_OUTPUT_DIR}/$<CONFIG>"
    )
endif()
