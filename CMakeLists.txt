cmake_minimum_required(VERSION 3.20)

set(PROJECT_NAME "BuriedPoint")

project(${PROJECT_NAME} VERSION 1.1.1.1)

option(BUILD_BURIED_SHARED_LIBS "build shared library" ON)
option(BUILD_BURIED_STATIC_LIBS "build static library" ON)

option(BUILD_BURIED_EXAMPLES "build examples" OFF)
option(BUILD_BURIED_TEST "build unittest" OFF)

option(BUILD_BURIED_FOR_MT "build for /MT" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
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

set(INCDIRS . lib ${CMAKE_BINARY_DIR})
include_directories(${INCDIRS})

message(STATUS "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}")

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
