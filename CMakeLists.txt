cmake_minimum_required(VERSION 3.10)
project(Job_App)

set(CMAKE_CXX_STANDARD 17)

add_executable(Job_App src/main.cpp src/sqlite3.c src/db.cpp)

target_include_directories(Job_App PRIVATE include/)
target_link_libraries(Job_App curl)