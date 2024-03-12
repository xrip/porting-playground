cmake_minimum_required(VERSION 3.22)

project(kiwi C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD 14)
set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -funroll-loops  -ffast-math  -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -funroll-loops  -ffast-math  -O2")

# INCLUDE FILES THAT SHOULD BE COMPILED:
file(GLOB_RECURSE SRC "src/*.cpp" "src/*.c")

message(STATUS "Add source files:")
foreach(SRC_FILE IN LISTS SRC)
    message(STATUS "${SRC_FILE}")
endforeach()
message(STATUS "")


add_executable(${PROJECT_NAME} ${SRC})

target_link_libraries(${PROJECT_NAME}
        winmm
)