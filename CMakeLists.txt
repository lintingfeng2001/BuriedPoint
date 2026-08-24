cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME "BuriedPoint")

project(${PROJECT_NAME} VERSION 1.1.1.1)

option(BUILD_BURIED_SHARED_LIBS "build shared library" ON)
option(BUILD_BURIED_STATIC_LIBS "build static library" ON)

option(BUILD_BURIED_EXAMPLES "build examples" OFF)
option(BUILD_BURIED_TEST "build unittest" OFF)

option(BUILD_BURIED_FOR_MT "build for /MT" OFF)
option(ENABLE_ASAN "enable AddressSanitizer instrumentation" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(ENABLE_ASAN)
    if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # MSVC Debug defaults include /RTC1, which is incompatible with ASan.
        foreach(language C CXX)
            string(REGEX REPLACE "[/-]RTC[^ ]*" ""
                CMAKE_${language}_FLAGS_DEBUG
                "${CMAKE_${language}_FLAGS_DEBUG}")
        endforeach()

        # Instrument every target, including static third-party dependencies.
        # Mixing instrumented objects with non-instrumented MSVC STL objects
        # triggers /FAILIFMISMATCH errors for ASan annotations.
        add_compile_options(
            /fsanitize=address
            /Zi
            /Oy-)
        add_link_options(/INCREMENTAL:NO)

        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            # Ninja links clang-cl targets through lld-link directly, so add
            # the runtime arguments that the clang-cl driver would inject.
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" --print-resource-dir
                OUTPUT_VARIABLE CLANG_RESOURCE_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE
                COMMAND_ERROR_IS_FATAL ANY)
            if(DEFINED ENV{VCToolsInstallDir})
                file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" VC_TOOLS_DIR)
                set(ASAN_RUNTIME_DIR "${VC_TOOLS_DIR}/lib/x64")
                set(ASAN_RUNTIME_DLL_DIR
                    "${VC_TOOLS_DIR}/bin/Hostx64/x64")
            else()
                set(ASAN_RUNTIME_DIR "${CLANG_RESOURCE_DIR}/lib/windows")
                set(ASAN_RUNTIME_DLL_DIR "${ASAN_RUNTIME_DIR}")
            endif()
            set(ASAN_DYNAMIC_RUNTIME
                "${ASAN_RUNTIME_DIR}/clang_rt.asan_dynamic-x86_64.lib")
            set(ASAN_RUNTIME_THUNK
                "${ASAN_RUNTIME_DIR}/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
            set(ASAN_RUNTIME_DLL
                "${ASAN_RUNTIME_DLL_DIR}/clang_rt.asan_dynamic-x86_64.dll")

            if(NOT EXISTS "${ASAN_DYNAMIC_RUNTIME}" OR
               NOT EXISTS "${ASAN_RUNTIME_THUNK}" OR
               NOT EXISTS "${ASAN_RUNTIME_DLL}")
                message(FATAL_ERROR
                    "clang-cl ASan runtime files were not found")
            endif()

            link_libraries(
                "${ASAN_DYNAMIC_RUNTIME}"
                "${ASAN_RUNTIME_THUNK}")
            add_link_options(
                /INCLUDE:__asan_seh_interceptor
                "/WHOLEARCHIVE:${ASAN_RUNTIME_THUNK}"
                /INFERASANLIBS:NO)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(
            -fsanitize=address
            -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
    else()
        message(FATAL_ERROR
            "ENABLE_ASAN is not supported by ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()

if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

    if(BUILD_BURIED_FOR_MT)
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    endif()

    # Generate PDB files for release builds.
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE
        "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

set(LIBS ${LIBS} winmm iphlpapi ws2_32 dbghelp Kernel32)

function(copy_mingw_runtime_dlls target_name)
    if(NOT MINGW)
        return()
    endif()

    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(runtime_dll
            libgcc_s_seh-1.dll
            libstdc++-6.dll
            libwinpthread-1.dll)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_BIN_DIR}/${runtime_dll}"
                "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM)
    endforeach()
endfunction()

function(copy_asan_runtime_dll target_name)
    if(NOT ENABLE_ASAN OR NOT DEFINED ASAN_RUNTIME_DLL)
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ASAN_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:${target_name}>"
        VERBATIM)
endfunction()

set(INCDIRS . lib ${CMAKE_BINARY_DIR})
include_directories(${INCDIRS})

message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")
message(STATUS "ENABLE_ASAN=${ENABLE_ASAN}")

if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_definitions(-DDEBUG)
else()
    add_definitions(-DNDEBUG)
endif()

add_subdirectory(src)

if(BUILD_BURIED_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_BURIED_TEST)
    include_directories(
        googletest/googletest
        googletest/googletest/include)
    add_subdirectory(googletest)
    add_subdirectory(tests)
endif()
