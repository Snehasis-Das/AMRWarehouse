cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(AMRSimulator LANGUAGES CXX)

set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
file(GLOB_RECURSE SOURCES "${SOURCE_DIR}/*.cpp")

find_package(Threads REQUIRED)

include_directories(
    C:/SFML-3.0.2-windows-vc17-64-bit/SFML-3.0.2/include
    ${PROJECT_SOURCE_DIR}/include
)

link_directories(
    C:/SFML-3.0.2-windows-vc17-64-bit/SFML-3.0.2/lib
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PUBLIC
    debug sfml-graphics-d
    debug sfml-window-d
    debug sfml-system-d

    optimized sfml-graphics
    optimized sfml-window
    optimized sfml-system
    Threads::Threads
    ws2_32
)