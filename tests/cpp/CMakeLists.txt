find_package(Threads REQUIRED)
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(sisl_cpp_tests
  test_description.cpp
  test_parser.cpp
  test_compiler.cpp
  test_runtime.cpp)
target_link_libraries(sisl_cpp_tests PRIVATE
  sisl::sisl
  GTest::gtest_main
  Threads::Threads)
target_compile_features(sisl_cpp_tests PRIVATE cxx_std_23)
target_compile_definitions(sisl_cpp_tests PRIVATE
  SISL_RISCV_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../riscv.isa")
gtest_discover_tests(sisl_cpp_tests TEST_PREFIX "sisl.cpp.")

if(SISL_INSTALL)
  set(SISL_INSTALL_TEST_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/install-prefix")
  if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    # Nix uses absolute GNUInstallDirs. DESTDIR is the only way to stage those
    # paths for a relocatability test.
    add_test(NAME sisl.cpp.install
      COMMAND "${CMAKE_COMMAND}" -E env
        "DESTDIR=${SISL_INSTALL_TEST_PREFIX}"
        "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}")
    set(SISL_INSTALL_TEST_PACKAGE_DIR
      "${SISL_INSTALL_TEST_PREFIX}${CMAKE_INSTALL_LIBDIR}/cmake/Sisl")
  else()
    add_test(NAME sisl.cpp.install
      COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
              --prefix "${SISL_INSTALL_TEST_PREFIX}")
    set(SISL_INSTALL_TEST_PACKAGE_DIR
      "${SISL_INSTALL_TEST_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/Sisl")
  endif()
  set_tests_properties(sisl.cpp.install PROPERTIES
    FIXTURES_SETUP sisl_installed)

  add_test(NAME sisl.cpp.consumer.configure
    COMMAND "${CMAKE_COMMAND}"
      -S "${CMAKE_CURRENT_SOURCE_DIR}/consumer"
      -B "${CMAKE_CURRENT_BINARY_DIR}/consumer-build"
      "-DSisl_DIR=${SISL_INSTALL_TEST_PACKAGE_DIR}"
      "-DBoost_DIR=${Boost_DIR}")
  set_tests_properties(sisl.cpp.consumer.configure PROPERTIES
    FIXTURES_REQUIRED sisl_installed
    FIXTURES_SETUP sisl_consumer_configured)

  add_test(NAME sisl.cpp.consumer.build
    COMMAND "${CMAKE_COMMAND}" --build
            "${CMAKE_CURRENT_BINARY_DIR}/consumer-build")
  set_tests_properties(sisl.cpp.consumer.build PROPERTIES
    FIXTURES_REQUIRED sisl_consumer_configured
    FIXTURES_SETUP sisl_consumer_built)

  add_test(NAME sisl.cpp.consumer.run
    COMMAND "${CMAKE_CURRENT_BINARY_DIR}/consumer-build/sisl_consumer")
  set_tests_properties(sisl.cpp.consumer.run PROPERTIES
    FIXTURES_REQUIRED sisl_consumer_built
    PASS_REGULAR_EXPRESSION "SISL installed consumer passed")
endif()

add_test(NAME sisl.cpp.source-consumer.configure
  COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_SOURCE_DIR}/source-consumer"
    -B "${CMAKE_CURRENT_BINARY_DIR}/source-consumer-build"
    "-DSISL_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    "-DBoost_DIR=${Boost_DIR}")
set_tests_properties(sisl.cpp.source-consumer.configure PROPERTIES
  FIXTURES_SETUP sisl_source_consumer_configured)

add_test(NAME sisl.cpp.source-consumer.build
  COMMAND "${CMAKE_COMMAND}" --build
          "${CMAKE_CURRENT_BINARY_DIR}/source-consumer-build")
set_tests_properties(sisl.cpp.source-consumer.build PROPERTIES
  FIXTURES_REQUIRED sisl_source_consumer_configured)
