cmake_minimum_required(VERSION 3.28)

project(fastpipe VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "fastpipe requires Windows")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

option(FASTPIPE_ENABLE_RUNTIME_STDIO_HOOK "Enable runtime injection of a matching stdio hook DLL" OFF)
option(FASTPIPE_BUILD_INJECTION_DLLS "Expose the multi-architecture injection DLL build target" ON)
set(FASTPIPE_INJECTION_ARCHITECTURES "x86;x64;ARM64;ARM64EC" CACHE STRING "Architectures built by fp_injection_dlls")
list(REMOVE_ITEM FASTPIPE_INJECTION_ARCHITECTURES ARM arm)

add_executable(fp
    src/cli_parser.cpp
    src/config.cpp
    src/main.cpp
    src/pipe_chain.cpp
    src/process_launcher.cpp
    src/stdio_injection.cpp
)

target_include_directories(fp PRIVATE src)
target_link_libraries(fp PRIVATE Shell32)

if(FASTPIPE_ENABLE_RUNTIME_STDIO_HOOK)
    set(DETOURS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Detours/src")
    add_library(fastpipe_detours STATIC
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
    target_include_directories(fastpipe_detours PUBLIC "${DETOURS_SOURCE_DIR}")
    target_compile_definitions(fastpipe_detours PRIVATE
        DETOUR_DEBUG=0
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0501
    )
    target_link_libraries(fp PRIVATE fastpipe_detours)
    target_include_directories(fp PRIVATE "${DETOURS_SOURCE_DIR}")
    target_compile_definitions(fp PRIVATE FASTPIPE_ENABLE_RUNTIME_STDIO_HOOK=1)
endif()

if(MSVC)
    target_compile_options(fp PRIVATE /W4 /permissive-)
endif()

if(FASTPIPE_BUILD_INJECTION_DLLS)
    include(ExternalProject)

    add_custom_target(fp_injection_dlls)

    foreach(injection_arch IN LISTS FASTPIPE_INJECTION_ARCHITECTURES)
        string(TOLOWER "${injection_arch}" injection_arch_lower)
        if(injection_arch STREQUAL "x86")
            set(injection_platform "Win32")
        else()
            set(injection_platform "${injection_arch}")
        endif()

        set(injection_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/injection/${injection_arch_lower}")
        set(injection_output_dir "${CMAKE_CURRENT_BINARY_DIR}/injection-artifacts/${injection_arch_lower}")
        set(injection_project "fp_stdio_hook_${injection_arch_lower}")

        ExternalProject_Add(${injection_project}
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/stdio_hook"
            BINARY_DIR "${injection_binary_dir}"
            DOWNLOAD_COMMAND ""
            UPDATE_COMMAND ""
            PATCH_COMMAND ""
            CONFIGURE_COMMAND
                "${CMAKE_COMMAND}"
                -S "${CMAKE_CURRENT_SOURCE_DIR}/src/stdio_hook"
                -B "${injection_binary_dir}"
                -G "${CMAKE_GENERATOR}"
                -A "${injection_platform}"
                "-DFASTPIPE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DFASTPIPE_INJECTION_ARCH=${injection_arch}"
                "-DFASTPIPE_OUTPUT_DIR=${injection_output_dir}"
            BUILD_COMMAND
                "${CMAKE_COMMAND}" --build "${injection_binary_dir}" --config "$<CONFIG>"
            INSTALL_COMMAND ""
            TEST_COMMAND ""
        )

        add_dependencies(fp_injection_dlls ${injection_project})
    endforeach()

    if(FASTPIPE_ENABLE_RUNTIME_STDIO_HOOK)
        add_dependencies(fp fp_injection_dlls)
    endif()
endif()

enable_testing()

add_executable(fp_tests
    tests/test_parser_config.cpp
    src/cli_parser.cpp
    src/config.cpp
)
target_include_directories(fp_tests PRIVATE src)
target_link_libraries(fp_tests PRIVATE Shell32)

if(MSVC)
    target_compile_options(fp_tests PRIVATE /W4 /permissive-)
endif()

add_test(NAME fp_parser_config COMMAND fp_tests)

add_executable(fp_fixture tests/pipe_fixture.cpp)
if(MSVC)
    target_compile_options(fp_fixture PRIVATE /W4 /permissive-)
endif()

add_test(
    NAME fp_pipeline
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pipeline_tests.ps1"
        -Fastpipe "$<TARGET_FILE:fp>"
        -Fixture "$<TARGET_FILE:fp_fixture>"
)
