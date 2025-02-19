cmake_minimum_required(VERSION 3.24)
project(message_thread)

set(CMAKE_CXX_STANDARD 11)

include_directories(${PROJECT_SOURCE_DIR})

add_executable(Demo main.cpp)
