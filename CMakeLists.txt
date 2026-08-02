cmake_minimum_required(VERSION 3.10)
project(Job_App)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(NOT DEFINED APP_VERSION OR APP_VERSION STREQUAL "")
    set(APP_VERSION "unknown")
endif()
add_compile_definitions(APP_VERSION="${APP_VERSION}")

add_executable(Job_App src/main.cpp src/sqlite3.c src/db.cpp src/http.cpp src/html.cpp src/config.cpp src/ai.cpp src/scraper.cpp src/fitcheck.cpp src/response.cpp src/routes.cpp src/scheduler.cpp src/export.cpp)

target_include_directories(Job_App PRIVATE include/)

find_package(ZLIB REQUIRED)
target_compile_definitions(Job_App PRIVATE CPPHTTPLIB_ZLIB_SUPPORT)
target_link_libraries(Job_App curl ZLIB::ZLIB)
