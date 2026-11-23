# R_ImGUI: IdlyEngine's editor look for any Dear ImGui application, and a kit for realtime rendering research.
#   rimgui/               the libraries: rimgui (theme, fonts, widgets, the application shell) and rimgui_rt (the
#                         research kit)
#   templates/starter/    StarterApp, a tool with the editor's look
#   templates/renderlab/  RenderLab, a renderer research application
# Building here builds the libraries and both templates:
#   cmake -B build
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(R_ImGUI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(rimgui)
add_subdirectory(templates/starter)
add_subdirectory(templates/renderlab)

# Tests: ctest --test-dir build. They open OpenGL windows that are never shown.
enable_testing()
add_subdirectory(tests)
