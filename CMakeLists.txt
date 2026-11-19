# R_ImGUI: IdlyEngine's editor look for any Dear ImGui application.
#   rimgui/     the library: theme, fonts, widgets, the application shell
#   template/   StarterApp, what tools/new_app.ps1 copies for a new application
# Building here builds the library and StarterApp:
#   cmake -B build
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(R_ImGUI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_subdirectory(rimgui)
add_subdirectory(template)
