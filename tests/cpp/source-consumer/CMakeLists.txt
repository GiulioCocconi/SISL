cmake_minimum_required(VERSION 3.24)
project(SislSourceConsumer LANGUAGES CXX)

# A parent build's global library default must remain under its control, while
# a vendored SISL can independently request a static build.
set(BUILD_SHARED_LIBS ON CACHE BOOL "Parent library default")
set(SISL_BUILD_SHARED_LIBS OFF CACHE BOOL "SISL linkage")
set(SISL_BUILD_TESTS OFF CACHE BOOL "SISL tests")
set(SISL_INSTALL OFF CACHE BOOL "SISL install rules")

add_subdirectory("${SISL_SOURCE_DIR}" sisl EXCLUDE_FROM_ALL)

if(NOT TARGET sisl::sisl OR NOT TARGET Sisl::Sisl)
  message(FATAL_ERROR "SISL did not expose its documented CMake targets")
endif()
if(NOT BUILD_SHARED_LIBS)
  message(FATAL_ERROR "SISL modified its parent's BUILD_SHARED_LIBS setting")
endif()
get_target_property(_sisl_type sisl TYPE)
if(NOT _sisl_type STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "SISL_BUILD_SHARED_LIBS=OFF did not create a static library")
endif()
get_target_property(_sisl_features sisl INTERFACE_COMPILE_FEATURES)
if(NOT "cxx_std_23" IN_LIST _sisl_features)
  message(FATAL_ERROR "SISL does not propagate its C++23 requirement")
endif()
if(TARGET sisl_cpp_tests)
  message(FATAL_ERROR "Vendored SISL unexpectedly enabled its own tests")
endif()
