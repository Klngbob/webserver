cmake_minimum_required(VERSION 2.8)
project(Tiny_Web_Server)

set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS "-std=c++11 -O0 -Wall -g -ggdb")

# 设置可执行文件和自定义库输出路径
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib)

set(WORK_SPACE_PATH ${PROJECT_SOURCE_DIR})
configure_file(
    ${PROJECT_SOURCE_DIR}/include/global_definition/global_definition.h.in
    ${PROJECT_SOURCE_DIR}/include/global_definition/global_definition.h
)

set(THIRD_LIBRARIES pthread mysqlclient)
file(GLOB_RECURSE SRCS "src/*.cpp")
message(STATUS "SRC_DIR_LIST = ${SRCS}")
list(REMOVE_ITEM SRCS "src/cgi.cpp")

# YAML 
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp)
include_directories(${YAML_CPP_INCLUDE_DIRS})
list(APPEND THIRD_LIBRARIES ${YAML_CPP_LIBRARIES})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
add_subdirectory(src)
# add_subdirectory(apps)

# add_executable(webserver apps/web_server.cpp)
# target_link_libraries(webserver server ${THIRD_LIBRARIES})

add_executable(webserver apps/main.cpp)
target_link_libraries(webserver server ${THIRD_LIBRARIES})

# 在build.sh中将它移入www/html/中...
add_executable(CGISQL.cgi apps/cgi.cpp)
target_link_libraries(CGISQL.cgi server ${THIRD_LIBRARIES})

# add_executable(echoserver apps/server.cpp)
# target_link_libraries(echoserver server)

# add_executable(echoclient apps/client.cpp)
# target_link_libraries(echoclient server)