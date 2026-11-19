# StarterApp: a new application with R_ImGUI's look. tools/new_app.ps1 copies this folder and renames StarterApp.
#   cmake -B build
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(StarterApp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The rimgui library: a copy inside this folder (what new_app.ps1 makes), or the R_ImGUI repository's own.
if(NOT TARGET rimgui)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rimgui/CMakeLists.txt")
		add_subdirectory(rimgui)
	else()
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../rimgui" "${CMAKE_BINARY_DIR}/rimgui")
	endif()
endif()

add_executable(StarterApp src/main.cpp)
target_link_libraries(StarterApp PRIVATE rimgui)
rimgui_windows_app(StarterApp)
if(MSVC)
	target_compile_options(StarterApp PRIVATE /W4 /permissive- /utf-8)
endif()
