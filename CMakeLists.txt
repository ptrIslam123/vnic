cmake_minimum_required(VERSION 3.16)

project(vnic)


file(GLOB_RECURSE SRC "src/*.cpp" "include/*.h")

add_executable(${PROJECT_NAME} ${SRC})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/include")

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 20              # или 17, 14, 11
    CXX_STANDARD_REQUIRED ON     # требовать именно этот стандарт
    CXX_EXTENSIONS OFF           # отключить расширения компилятора (опционально)
)
