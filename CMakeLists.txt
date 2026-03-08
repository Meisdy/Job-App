cmake_minimum_required(VERSION 4.1)
project(Job_App)

set(CMAKE_CXX_STANDARD 17)

add_executable(Job_App src/main.cpp include/httplib.h src/sqlite3.c include/json.hpp)

target_include_directories(Job_App PRIVATE include/)
target_link_libraries(Job_App ws2_32)