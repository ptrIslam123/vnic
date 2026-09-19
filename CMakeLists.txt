cmake_minimum_required(VERSION 3.18)

project(vnic)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

function(make_example target_name src)
    add_executable(${target_name} ${src})
    target_link_libraries(${target_name} PUBLIC ${PROJECT_NAME})

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
    )
endfunction()

function(make_test target_name src)
    add_executable(${target_name} ${src})
    target_link_libraries(${target_name} PRIVATE ${PROJECT_NAME} GTest::gtest_main)

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
    )

    include(GoogleTest)
    gtest_discover_tests(${target_name})
endfunction()

file(GLOB_RECURSE VNIC_SRC "src/*.cpp")
file(GLOB_RECURSE VNIC_HDRS "include/*.h")
file(GLOB_RECURSE VNIC_UNIT_TEST_SRC "tests/unit/*.cpp")
add_library(${PROJECT_NAME} ${VNIC_SRC} ${VNIC_HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/include")

set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vnic"
)

# ----------------------- examples -----------------------
make_example(base-nic "examples/base-nic/main.cpp")

# ----------------------- unit tests -----------------------
make_test(unit-tests "${VNIC_UNIT_TEST_SRC}")