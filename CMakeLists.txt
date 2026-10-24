cmake_minimum_required(VERSION 3.24)

project(vnic
    VERSION 0.1.0
    DESCRIPTION "Userspace NIC emulation library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VNIC_BUILD_TESTS    "Build unit tests"                 ON)
option(VNIC_BUILD_EXAMPLES "Build examples"                   ON)
option(VNIC_SANITIZE       "Enable AddressSanitizer and UBSan" OFF)
option(VNIC_COVERAGE       "Enable gcov coverage instrumentation" OFF)
option(VNIC_WERROR         "Treat warnings as errors"         OFF)
option(VNIC_BUILD_DOCS     "Build API documentation (needs Doxygen)" OFF)
option(VNIC_FORMAT_TARGETS "Add clang-format targets (needs clang-format)" ON)

# Default to a release build in single-config generators.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# VNic has no external runtime dependencies; GoogleTest is fetched only for tests.
find_package(Threads REQUIRED)

if(VNIC_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

file(GLOB_RECURSE VNIC_SRC  CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE VNIC_HDRS CONFIGURE_DEPENDS "include/*.h")

add_library(vnic ${VNIC_SRC} ${VNIC_HDRS})
add_library(vnic::vnic ALIAS vnic)
target_compile_features(vnic PUBLIC cxx_std_23)

target_include_directories(vnic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(vnic PUBLIC Threads::Threads)

target_compile_options(vnic PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<BOOL:${VNIC_WERROR}>:-Werror>
)

if(VNIC_SANITIZE)
    target_compile_options(vnic PUBLIC
        $<BUILD_INTERFACE:-fsanitize=address,undefined>
        $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(vnic PUBLIC $<BUILD_INTERFACE:-fsanitize=address,undefined>)
endif()

# gcov instrumentation for the library only. Consumers such as unit-tests link
# the gcov runtime, but their own sources are not instrumented: test/example
# coverage is excluded from the report. BUILD_INTERFACE keeps the flags out of
# the installed target.
if(VNIC_COVERAGE)
    target_compile_options(vnic PUBLIC $<BUILD_INTERFACE:--coverage>)
    target_link_options(vnic PUBLIC $<BUILD_INTERFACE:--coverage>)
endif()

set_target_properties(vnic PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
)

install(TARGETS vnic
    EXPORT vnicTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES
    LICENSE
    README.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

install(EXPORT vnicTargets
    FILE vnicTargets.cmake
    NAMESPACE vnic::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vnic
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vnicConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vnicConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vnic
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/vnicConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/vnicConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/vnicConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vnic
)

function(make_example target_name src)
    add_executable(${target_name} ${src})
    target_link_libraries(${target_name} PRIVATE vnic::vnic)
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
    )
endfunction()

if(VNIC_BUILD_EXAMPLES)
    make_example(base-nic "examples/base-nic/main.cpp")
endif()

if(VNIC_BUILD_TESTS)
    enable_testing()

    file(GLOB_RECURSE VNIC_UNIT_TEST_SRC CONFIGURE_DEPENDS "tests/unit/*.cpp")

    add_executable(unit-tests ${VNIC_UNIT_TEST_SRC})
    target_link_libraries(unit-tests PRIVATE vnic::vnic GTest::gtest_main)
    set_target_properties(unit-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    include(GoogleTest)
    gtest_discover_tests(unit-tests)
endif()

# Build with -DVNIC_COVERAGE=ON, then run:
#     cmake --build build --target coverage
# Requires lcov and genhtml (gcov is used by lcov to read the counters).
if(VNIC_COVERAGE AND VNIC_BUILD_TESTS)
    find_program(LCOV_EXECUTABLE NAMES lcov)
    find_program(GENHTML_EXECUTABLE NAMES genhtml)

    if(LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        set(VNIC_COVERAGE_INFO "${CMAKE_BINARY_DIR}/coverage.info")
        set(VNIC_COVERAGE_FILTERED "${CMAKE_BINARY_DIR}/coverage.filtered.info")
        set(VNIC_COVERAGE_HTML "${CMAKE_BINARY_DIR}/coverage-html")
        # Capture only the library objects. Tests/examples are not part of the
        # report, and scanning their counters can trip lcov on gcov version
        # mismatches in GoogleTest/test sources.
        set(VNIC_COVERAGE_DIR "${CMAKE_BINARY_DIR}/CMakeFiles/vnic.dir")

        # lcov 2.x treats some GCC 15 line/function annotations as hard errors.
        execute_process(
            COMMAND ${LCOV_EXECUTABLE} --version
            OUTPUT_VARIABLE VNIC_LCOV_VERSION
            ERROR_VARIABLE VNIC_LCOV_VERSION
        )
        if(VNIC_LCOV_VERSION MATCHES "version ([0-9]+)\.")
            set(VNIC_LCOV_VERSION_MAJOR ${CMAKE_MATCH_1})
        else()
            set(VNIC_LCOV_VERSION_MAJOR 1)
        endif()
        if(VNIC_LCOV_VERSION_MAJOR GREATER_EQUAL 2)
            set(VNIC_LCOV_IGNORE --ignore-errors inconsistent,unused,mismatch)
        else()
            set(VNIC_LCOV_IGNORE "")
        endif()

        add_custom_target(coverage
            COMMAND ${LCOV_EXECUTABLE} --zerocounters --directory ${VNIC_COVERAGE_DIR}
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            COMMAND ${LCOV_EXECUTABLE} --capture --directory ${VNIC_COVERAGE_DIR}
                    ${VNIC_LCOV_IGNORE}
                    --output-file ${VNIC_COVERAGE_INFO}
            COMMAND ${LCOV_EXECUTABLE} --remove ${VNIC_COVERAGE_INFO}
                    "/usr/*"
                    ${VNIC_LCOV_IGNORE}
                    --output-file ${VNIC_COVERAGE_FILTERED}
            COMMAND ${GENHTML_EXECUTABLE} ${VNIC_COVERAGE_FILTERED}
                    --output-directory ${VNIC_COVERAGE_HTML}
                    --legend --demangle --show-details
            DEPENDS unit-tests
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running tests and generating coverage report"
            VERBATIM
            USES_TERMINAL
        )
    else()
        message(WARNING
            "VNIC_COVERAGE=ON but lcov/genhtml not found: "
            "'coverage' target disabled. Install lcov to generate the report.")
    endif()
elseif(VNIC_COVERAGE AND NOT VNIC_BUILD_TESTS)
    message(WARNING "VNIC_COVERAGE=ON requires VNIC_BUILD_TESTS=ON: 'coverage' target disabled.")
endif()

# `format` rewrites sources in place; `check-format` only verifies (non-zero
# exit if reformatting is needed). Neither is part of the default build.
if(VNIC_FORMAT_TARGETS)
    find_program(CLANG_FORMAT NAMES clang-format clang-format-19 clang-format-18 clang-format-17)

    if(CLANG_FORMAT)
        file(GLOB_RECURSE VNIC_FORMAT_SOURCES CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        )

        add_custom_target(format
            COMMAND ${CLANG_FORMAT} -i ${VNIC_FORMAT_SOURCES}
            COMMENT "Formatting sources with clang-format"
            VERBATIM
        )

        add_custom_target(check-format
            COMMAND ${CLANG_FORMAT} --dry-run --Werror ${VNIC_FORMAT_SOURCES}
            COMMENT "Checking source formatting with clang-format"
            VERBATIM
        )
    else()
        message(STATUS "clang-format not found: 'format'/'check-format' targets disabled")
    endif()
endif()

# The `vnic-docs` target is NOT part of the default build: Doxygen runs only
# when it is built explicitly, e.g.
#     cmake --build build --target vnic-docs
# A plain `cmake --build build` does not regenerate the documentation.
# To regenerate on every build instead, add: add_dependencies(vnic vnic-docs)
if(VNIC_BUILD_DOCS)
    find_package(Doxygen REQUIRED)

    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_OUTPUT_LANGUAGE English)
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dock/api")
    doxygen_add_docs(vnic-docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        ${CMAKE_CURRENT_SOURCE_DIR}/dock/architecture.md
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
