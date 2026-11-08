cmake_minimum_required(VERSION 3.22)
project(ebovengine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Single source of truth for the engine lives in <repo>/engine/code.cpp
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
add_library(ebovengine SHARED ${REPO_ROOT}/engine/code.cpp jni_bridge.cpp)
target_compile_definitions(ebovengine PRIVATE EBOV_NO_MAIN)
target_compile_options(ebovengine PRIVATE -O2)
