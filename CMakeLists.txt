# Copyright 2026 InsightOS
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.12)
project(AbilityFramework VERSION 2.0.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

option(GLOG_USE_GLOG_EXPORT "如果在termux上编译则需要打开" OFF)

# 低版本g++ 编译兼容
if(CMAKE_CXX_COMPILER_ID STREQUAL GNU)
  if(CMAKE_CXX_COMPILER_VERSION LESS 11)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fcoroutines>)
  endif()
endif()

if(GLOG_USE_GLOG_EXPORT)
  add_compile_definitions(GLOG_USE_GLOG_EXPORT)
endif()

# 获取 Git 提交哈希
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 获取构建时间
execute_process(
    COMMAND date "+%Y-%m-%d"
    OUTPUT_VARIABLE BUILD_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# 使用 configure_file 生成 version.hpp
configure_file(
    ${CMAKE_SOURCE_DIR}/include/util/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/version.hpp
    @ONLY
)

# 包含生成的头文件路径
include_directories(${CMAKE_BINARY_DIR}/generated)

# 使用 apt 等包管理器安装的包
find_package(PkgConfig REQUIRED)
pkg_search_module(LibUV REQUIRED libuv)
pkg_search_module(LibGlog REQUIRED libglog)
pkg_search_module(NlohmannJson REQUIRED nlohmann_json)
pkg_search_module(YamlCpp REQUIRED yaml-cpp)
pkg_search_module(SQLite3 REQUIRED sqlite3)
pkg_search_module(Hiredis REQUIRED hiredis)


# 以下包需要手动下载源码,并使用cmake安装

# cpp-httplib 虽然是个头文件库,但是ubuntu apt中的cpp-httplib版本太低了,需要从github上安装较新的版本 uvw
# 是libuv的C++包装库
find_package(uvw REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)
find_package(stduuid REQUIRED)
find_package(CURL REQUIRED)
find_package(miniz REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(cxxopts REQUIRED)
find_package(jwt-cpp REQUIRED)

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(
  ${LibUV_INCLUDE_DIRS} ${LibGlog_INCLUDE_DIRS} ${NlohmannJson_INCLUDE_DIRS}
  ${YamlCpp_INCLUDE_DIRS} ${SQLite3_INCLUDE_DIRS} ${Hiredis_INCLUDE_DIRS})

add_subdirectory(test) # 用来放一些测试用的函数文件

file(GLOB_RECURSE abilityframework_srcs_except_main src/*.cpp)
list(REMOVE_ITEM abilityframework_srcs_except_main
     ${PROJECT_SOURCE_DIR}/src/main.cpp)
# 这是囊括了能力框架所有文件(除了主函数),形成的一个库 编写测试的时候可以直接依赖这个库,免得复制框架文件
add_library(AbilityFrameworkAux ${abilityframework_srcs_except_main})
target_link_libraries(
  AbilityFrameworkAux
  PUBLIC nlohmann_json_schema_validator
         miniz
         cxxopts::cxxopts
         dwarf
         zstd
         ZLIB::ZLIB
         ${LibUV_LIBRARIES}
         ${LibGlog_LIBRARIES}
         ${NlohmannJson_LIBRARIES}
         ${YamlCpp_LIBRARIES}
         ${SQLite3_LIBRARIES}
         ${Hiredis_LIBRARIES}
         CURL::libcurl   # 添加 libcurl 的链接
         jwt-cpp)  

# 生成abilityframework可执行文件
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(AbilityFramework AbilityFrameworkAux OpenSSL::SSL OpenSSL::Crypto)

# 设置doxygen

find_package(Doxygen OPTIONAL_COMPONENTS mscgen dia dot)
if(DOXYGEN_FOUND)
  message(
    STATUS
      "doxygen ${DOXYGEN_VERSION} found, use build target 'doxygen' for building docs"
  )
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_MAN NO)

  doxygen_add_docs(doxygen ${PROJECT_SOURCE_DIR} COMMENT "Generate html pages")

else()
  message(WARNING "doxygen not found")
endif()