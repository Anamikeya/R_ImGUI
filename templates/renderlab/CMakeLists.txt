# RenderLab: a realtime rendering research application. tools/new_app.ps1 -Template renderlab copies this folder and
# renames RenderLab. Shaders are in shaders/; while the program runs, saving one rebuilds it.
#   cmake -B build
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(RenderLab LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The rimgui library: a copy inside this folder (what new_app.ps1 makes), or the R_ImGUI repository's own.
if(NOT TARGET rimgui)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rimgui/CMakeLists.txt")
		add_subdirectory(rimgui)
	else()
		add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../rimgui" "${CMAKE_BINARY_DIR}/rimgui")
	endif()
endif()

add_executable(RenderLab src/main.cpp)
target_link_libraries(RenderLab PRIVATE rimgui_rt)
rimgui_add_shaders(RenderLab shaders)
rimgui_windows_app(RenderLab)
if(MSVC)
	target_compile_options(RenderLab PRIVATE /W4 /permissive- /utf-8)
endif()
