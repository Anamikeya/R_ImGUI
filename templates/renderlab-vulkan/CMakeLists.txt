# RenderLabVulkan: RenderLab on Vulkan, the research application with techniques written in Vulkan
# (rimgui/rt/vk/lab.h). Needs the Vulkan SDK (VULKAN_SDK set by its installer). tools/new_app.ps1 -Template
# renderlab-vulkan copies this folder and renames RenderLabVulkan. Shaders are in shaders/; while the program runs,
# saving one rebuilds it.
#   cmake -B build
#   cmake --build build --config Release
cmake_minimum_required(VERSION 3.24)
project(RenderLabVulkan LANGUAGES C CXX)

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

if(NOT RIMGUI_HAS_VULKAN)
	message(FATAL_ERROR "RenderLabVulkan needs the Vulkan SDK: install it from https://vulkan.lunarg.com (it sets VULKAN_SDK), "
		"or pass -DRIMGUI_VULKAN_SDK=<folder>.")
endif()

add_executable(RenderLabVulkan src/main.cpp)
target_link_libraries(RenderLabVulkan PRIVATE rimgui_rt_vk)
# Beside the OpenGL RenderLab in the repository's build, its own folder, so the two shader folders stay apart.
if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
	set_target_properties(RenderLabVulkan PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/renderlab-vulkan")
endif()
rimgui_add_shaders(RenderLabVulkan shaders)
rimgui_vulkan_app(RenderLabVulkan)
rimgui_windows_app(RenderLabVulkan)
if(MSVC)
	target_compile_options(RenderLabVulkan PRIVATE /W4 /permissive- /utf-8)
endif()
