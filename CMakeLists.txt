cmake_minimum_required(VERSION 3.15)

project(m8emu)
set(APP_NAME m8emu)
set(CMAKE_CXX_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    message(STATUS "Defaulting to build RelWithDebInfo")
endif()

add_subdirectory(externals/dynarmic)

set(CAPSTONE_ARCHITECTURE_DEFAULT OFF)
set(CAPSTONE_ARM_SUPPORT ON)
add_subdirectory(externals/capstone)

set(UVW_BUILD_LIBS ON)
add_subdirectory(externals/uvw)

add_subdirectory(ext)


file(READ firmware.yaml FIRMWARE_YAML)
configure_file(src/firmware.yaml.h.in firmware.yaml.h @ONLY)
add_library(headers INTERFACE)
target_include_directories(headers INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/" "${CMAKE_CURRENT_BINARY_DIR}/")

file(GLOB SRC "src/*.cpp")
add_executable(${APP_NAME} ${SRC})
target_link_libraries(${APP_NAME} dynarmic ihex ext headers uvw cqueue)
