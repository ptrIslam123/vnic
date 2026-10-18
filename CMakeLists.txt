cmake_minimum_required(VERSION 3.18)

project(vnic
    VERSION 0.1.0
    DESCRIPTION "Userspace NIC emulation library"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -------------------------------------------------------------------- options
option(VNIC_BUILD_TESTS    "Build unit tests"                 ON)
option(VNIC_BUILD_EXAMPLES "Build examples"                   ON)
option(VNIC_SANITIZE       "Enable AddressSanitizer and UBSan" OFF)
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

# -------------------------------------------------------------------- deps
# VNic has no external runtime dependencies; GoogleTest is fetched only for tests.
if(VNIC_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# -------------------------------------------------------------------- library
file(GLOB_RECURSE VNIC_SRC  CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE VNIC_HDRS CONFIGURE_DEPENDS "include/*.h")

add_library(vnic ${VNIC_SRC} ${VNIC_HDRS})
add_library(vnic::vnic ALIAS vnic)

target_include_directories(vnic PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_options(vnic PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<BOOL:${VNIC_WERROR}>:-Werror>
)

if(VNIC_SANITIZE)
    target_compile_options(vnic PUBLIC
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
    )
    target_link_options(vnic PUBLIC -fsanitize=address,undefined)
endif()

set_target_properties(vnic PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
)

# -------------------------------------------------------------------- install
install(TARGETS vnic
    EXPORT vnicTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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

# -------------------------------------------------------------------- examples
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

# -------------------------------------------------------------------- tests
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

# -------------------------------------------------------------------- format
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

# -------------------------------------------------------------------- docs
# The `vnic-docs` target is NOT part of the default build: Doxygen runs only
# when it is built explicitly, e.g.
#     cmake --build build --target vnic-docs
# A plain `cmake --build build` does not regenerate the documentation.
# To regenerate on every build instead, add: add_dependencies(vnic vnic-docs)
if(VNIC_BUILD_DOCS)
    find_package(Doxygen REQUIRED)

    set(DOXYGEN_PROJECT_NUMBER "${PROJECT_VERSION}")
    set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dock/api")
    doxygen_add_docs(vnic-docs
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/README.md
        ${CMAKE_CURRENT_SOURCE_DIR}/dock/архитектура.md
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
