# Dear ImGui (docking branch) and GLFW, pinned to exact commits.
#
# By default they are fetched from GitHub on the first configure. To build without a network, point at local copies:
#   cmake -B build -DRIMGUI_IMGUI_DIR=D:/libs/imgui -DRIMGUI_GLFW_DIR=D:/libs/glfw
# The pins match the commits IdlyEngine's editor is built and tested with.
include_guard(GLOBAL)
include(FetchContent)

set(RIMGUI_IMGUI_COMMIT d88f238d1d363ef387382b2e29ad12cf96f826ac) # v1.93.0-docking + 10
set(RIMGUI_GLFW_COMMIT 92dcf4ce74f2e2554a98fea09be7c705c17daa5a)
set(RIMGUI_IMGUI_DIR "" CACHE PATH "A local Dear ImGui checkout (docking branch); empty: fetch the pinned commit")
set(RIMGUI_GLFW_DIR "" CACHE PATH "A local GLFW checkout; empty: fetch the pinned commit")

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

if(RIMGUI_GLFW_DIR)
	add_subdirectory("${RIMGUI_GLFW_DIR}" "${CMAKE_BINARY_DIR}/_deps/glfw" EXCLUDE_FROM_ALL SYSTEM)
	set(_glfw "${RIMGUI_GLFW_DIR}")
else()
	FetchContent_Declare(rimgui_glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG ${RIMGUI_GLFW_COMMIT})
	FetchContent_MakeAvailable(rimgui_glfw)
	set(_glfw "${rimgui_glfw_SOURCE_DIR}")
endif()
# GLFW ships an OpenGL loader (glad) and stb_image_write in deps/; the research kit uses both.
if(NOT TARGET rimgui_glfw_deps)
	add_library(rimgui_glfw_deps INTERFACE)
	target_include_directories(rimgui_glfw_deps SYSTEM INTERFACE "${_glfw}/deps")
endif()

if(RIMGUI_IMGUI_DIR)
	set(_imgui "${RIMGUI_IMGUI_DIR}")
else()
	FetchContent_Declare(rimgui_imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG ${RIMGUI_IMGUI_COMMIT})
	FetchContent_MakeAvailable(rimgui_imgui)
	set(_imgui "${rimgui_imgui_SOURCE_DIR}")
endif()

if(NOT TARGET rimgui_dear_imgui)
	add_library(rimgui_dear_imgui STATIC
		"${_imgui}/imgui.cpp"
		"${_imgui}/imgui_demo.cpp"
		"${_imgui}/imgui_draw.cpp"
		"${_imgui}/imgui_tables.cpp"
		"${_imgui}/imgui_widgets.cpp"
		"${_imgui}/backends/imgui_impl_glfw.cpp"
		"${_imgui}/backends/imgui_impl_opengl3.cpp"
	)
	target_include_directories(rimgui_dear_imgui SYSTEM PUBLIC "${_imgui}" "${_imgui}/backends")
	find_package(OpenGL REQUIRED)
	target_link_libraries(rimgui_dear_imgui PUBLIC glfw OpenGL::GL)
	target_compile_features(rimgui_dear_imgui PUBLIC cxx_std_17)
endif()

# Vulkan, from the Vulkan SDK (https://vulkan.lunarg.com): on when the SDK is installed. It gives the headers, Volk
# (the function loader), VMA (memory), shaderc (GLSL to SPIR-V) and DXC (HLSL to SPIR-V).
set(RIMGUI_VULKAN AUTO CACHE STRING "Build the Vulkan backend and kit: ON, OFF, or AUTO (when the Vulkan SDK is found)")
set(RIMGUI_VULKAN_SDK "$ENV{VULKAN_SDK}" CACHE PATH "The Vulkan SDK's folder; empty: the VULKAN_SDK environment variable")
# Global (a cache entry), so the application's and the tests' folders see it too.
set(RIMGUI_HAS_VULKAN OFF CACHE INTERNAL "")
if(NOT RIMGUI_VULKAN STREQUAL "OFF")
	if(RIMGUI_VULKAN_SDK AND EXISTS "${RIMGUI_VULKAN_SDK}/Include/vulkan/vulkan.h" AND EXISTS "${RIMGUI_VULKAN_SDK}/Include/Volk/volk.c")
		set(RIMGUI_HAS_VULKAN ON CACHE INTERNAL "")
	elseif(RIMGUI_VULKAN STREQUAL "ON")
		message(FATAL_ERROR "RIMGUI_VULKAN is ON but no Vulkan SDK was found: install it from https://vulkan.lunarg.com "
			"(the VULKAN_SDK environment variable then names it), or set RIMGUI_VULKAN_SDK")
	else()
		message(STATUS "R_ImGUI: no Vulkan SDK found, so only OpenGL is built (install it from https://vulkan.lunarg.com for Vulkan)")
	endif()
endif()
if(RIMGUI_HAS_VULKAN AND NOT TARGET rimgui_volk)
	message(STATUS "R_ImGUI: Vulkan from ${RIMGUI_VULKAN_SDK}")
	add_library(rimgui_volk STATIC "${RIMGUI_VULKAN_SDK}/Include/Volk/volk.c")
	target_include_directories(rimgui_volk SYSTEM PUBLIC "${RIMGUI_VULKAN_SDK}/Include" "${RIMGUI_VULKAN_SDK}/Include/Volk")
	# Every Vulkan call goes through Volk's function pointers: no link to vulkan-1, and extension functions just work.
	target_compile_definitions(rimgui_volk PUBLIC VK_NO_PROTOTYPES)
	if(WIN32)
		target_compile_definitions(rimgui_volk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX)
	endif()
	add_library(rimgui_imgui_vulkan STATIC "${_imgui}/backends/imgui_impl_vulkan.cpp")
	target_link_libraries(rimgui_imgui_vulkan PUBLIC rimgui_dear_imgui rimgui_volk)
	target_compile_definitions(rimgui_imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
endif()
