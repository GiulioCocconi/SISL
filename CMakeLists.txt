cmake_minimum_required(VERSION 3.24)

project(Sisl VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(SISL_BUILD_TESTS "Build the SISL C++ tests" "${PROJECT_IS_TOP_LEVEL}")
option(SISL_INSTALL "Generate SISL install and package rules" "${PROJECT_IS_TOP_LEVEL}")
option(SISL_BUILD_SHARED_LIBS "Build SISL as a shared library" ON)
option(SISL_FETCH_LEXY "Fetch pinned lexy when no package is installed" ON)

find_package(Boost REQUIRED)
find_package(lexy 2025.05 CONFIG QUIET)

if(NOT lexy_FOUND)
  if(NOT SISL_FETCH_LEXY)
    message(FATAL_ERROR
      "lexy 2025.05 was not found. Install lexy 2025.05.0 or set SISL_FETCH_LEXY=ON.")
  endif()

  include(FetchContent)
  set(LEXY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(LEXY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  set(LEXY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LEXY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LEXY_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(LEXY_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(lexy
    URL "https://github.com/foonathan/lexy/archive/refs/tags/v2025.05.0.tar.gz"
    URL_HASH "SHA256=ae867846b890b7564d633cf28d39dfc4938fe7c54515126f720c1762de5eab30"
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
  FetchContent_MakeAvailable(lexy)
endif()

if(NOT TARGET foonathan::lexy::core)
  message(FATAL_ERROR "lexy does not provide foonathan::lexy::core")
endif()

if(SISL_BUILD_SHARED_LIBS)
  set(SISL_LIBRARY_TYPE SHARED)
else()
  set(SISL_LIBRARY_TYPE STATIC)
endif()

add_library(sisl ${SISL_LIBRARY_TYPE}
  src/common/source_map.cpp
  src/common/diagnostics.cpp
  src/parser/parser.cpp
  src/compiler/compiler.cpp
  src/compiler/layout.cpp
  src/compiler/instruction.cpp
  src/runtime/model.cpp
  src/runtime/runtime.cpp)
add_library(sisl::sisl ALIAS sisl)
add_library(Sisl::Sisl ALIAS sisl)

set_target_properties(sisl PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${PROJECT_VERSION_MAJOR}"
  EXPORT_NAME sisl)

target_compile_definitions(sisl PRIVATE SISL_BUILDING_LIBRARY)
if(NOT SISL_BUILD_SHARED_LIBS)
  target_compile_definitions(sisl PUBLIC SISL_STATIC)
endif()
target_compile_features(sisl PUBLIC cxx_std_23)
target_include_directories(sisl
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE src)
target_link_libraries(sisl
  PUBLIC Boost::headers
  PRIVATE "$<BUILD_INTERFACE:foonathan::lexy::core>")

if(SISL_INSTALL)
  install(TARGETS sisl EXPORT SislTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/SislConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion)
  configure_package_config_file(cmake/SislConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SislConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sisl")
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/SislConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/SislConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sisl")
  install(EXPORT SislTargets FILE SislTargets.cmake NAMESPACE sisl::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Sisl")
endif()

if(SISL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()
