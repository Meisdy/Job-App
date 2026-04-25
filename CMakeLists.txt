cmake_minimum_required(VERSION 3.10)
project(Job_App)

set(CMAKE_CXX_STANDARD 17)

execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_HASH)
    set(GIT_HASH "unknown")
endif()
add_compile_definitions(APP_VERSION="${GIT_HASH}")

add_executable(Job_App src/main.cpp src/sqlite3.c src/db.cpp)

target_include_directories(Job_App PRIVATE include/)
target_link_libraries(Job_App curl)
