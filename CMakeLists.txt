cmake_minimum_required(VERSION 3.14...3.22)

project(HelloDiligent CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(DiligentCore)
# add_subdirectory(libs/filabridge)
# add_subdirectory(libs/filaflat)
# add_subdirectory(libs/math)
# add_subdirectory(libs/utils)

add_executable(HelloDiligent WIN32 HelloDiligent.cpp)
target_compile_options(HelloDiligent PRIVATE -DUNICODE)

target_link_libraries(HelloDiligent
PRIVATE
    Diligent-GraphicsEngineD3D11-shared
    Diligent-GraphicsEngineOpenGL-shared
    Diligent-GraphicsEngineD3D12-shared
    Diligent-GraphicsEngineVk-shared
)
# target_link_libraries(HelloDiligent PUBLIC math)
# target_link_libraries(HelloDiligent PUBLIC utils)
# target_link_libraries(HelloDiligent PUBLIC filaflat)
# target_link_libraries(HelloDiligent PUBLIC filabridge)

copy_required_dlls(HelloDiligent)