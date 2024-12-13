cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_TAG        v1.15.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(cqueue
  GIT_REPOSITORY https://github.com/torrentg/cqueue.git
  GIT_TAG        1.0.6
)
FetchContent_Populate(cqueue)
add_library(cqueue INTERFACE)
target_include_directories(cqueue INTERFACE ${cqueue_SOURCE_DIR})

FetchContent_Declare(ihex
  GIT_REPOSITORY https://github.com/arkku/ihex.git
  GIT_TAG        509781c28ef0ce7dc02361588e6b4cf6a7ef818b
)
FetchContent_Populate(ihex)
add_library(ihex ${ihex_SOURCE_DIR}/kk_ihex_read.c)
target_include_directories(ihex PUBLIC ${ihex_SOURCE_DIR})


FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

file(GLOB SRC "*.cpp")
add_library(ext ${SRC})
target_link_libraries(ext PRIVATE dynarmic merry::mcl capstone spdlog::spdlog cqueue)
target_link_libraries(ext PUBLIC ihex yaml-cpp)
target_compile_definitions(ext PRIVATE MCL_IGNORE_ASSERTS=1)
