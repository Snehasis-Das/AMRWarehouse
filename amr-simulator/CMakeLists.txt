cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(AMRSimulator LANGUAGES CXX)

set(SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
file(GLOB_RECURSE SOURCES "${SOURCE_DIR}/*.cpp")

include_directories(
    C:/glew-2.1.0/glew-2.1.0/include
    C:/glfw-3.4/glfw-3.4.bin.WIN64/include
    C:/GLM/g-truc-glm-0af55cc
    ${PROJECT_SOURCE_DIR}/include
)

link_directories(
    C:/glew-2.1.0/glew-2.1.0/lib/Release/x64
    C:/glfw-3.4/glfw-3.4.bin.WIN64/lib-vc2022
)

add_executable(${PROJECT_NAME} ${SOURCES})

add_definitions(-DGLEW_STATIC)

target_link_libraries(${PROJECT_NAME} glfw3 glew32s opengl32 gdi32)