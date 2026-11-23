# rimgui: R_ImGUI's theme, widgets and application shell, as one static library.
# An application adds this folder and links rimgui:
#   add_subdirectory(rimgui)
#   target_link_libraries(my_app PRIVATE rimgui)
cmake_minimum_required(VERSION 3.24)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/RImGuiDependencies.cmake")

add_library(rimgui STATIC
	include/rimgui/app.h
	include/rimgui/theme.h
	include/rimgui/widgets.h
	src/app.cpp
	src/theme.cpp
	src/widgets.cpp
)
target_include_directories(rimgui PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_link_libraries(rimgui PUBLIC rimgui_dear_imgui)
if(RIMGUI_HAS_VULKAN)
	# The application shell on Vulkan as well: AppConfig::backend, or --vulkan on the command line.
	target_sources(rimgui PRIVATE include/rimgui/vulkan.h src/app_vulkan.cpp)
	target_link_libraries(rimgui PUBLIC rimgui_imgui_vulkan)
	target_compile_definitions(rimgui PUBLIC RIMGUI_HAS_VULKAN=1)
endif()
target_compile_features(rimgui PUBLIC cxx_std_17)
if(MSVC)
	target_compile_options(rimgui PRIVATE /W4 /permissive- /utf-8)
	target_compile_definitions(rimgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
	target_compile_options(rimgui PRIVATE -Wall -Wextra -Wpedantic)
endif()

# rimgui_rt_core: what both research kits share, with no graphics API: math, the camera, settings, images, meshes and
# model files read into CPU data.
add_library(rimgui_rt_core STATIC
	include/rimgui/rt/camera.h
	include/rimgui/rt/image.h
	include/rimgui/rt/math.h
	include/rimgui/rt/mesh_data.h
	include/rimgui/rt/model_data.h
	include/rimgui/rt/settings.h
	src/rt/camera.cpp
	src/rt/image.cpp
	src/rt/mesh_data.cpp
	src/rt/model_data.cpp
	src/rt/settings.cpp
)
target_link_libraries(rimgui_rt_core PUBLIC rimgui)
# stb_image and cgltf, kept in this repository (third_party/README.md).
target_include_directories(rimgui_rt_core SYSTEM PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/stb" "${CMAKE_CURRENT_LIST_DIR}/third_party/cgltf")
if(MSVC)
	target_compile_options(rimgui_rt_core PRIVATE /W4 /permissive- /utf-8)
	target_compile_definitions(rimgui_rt_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(rimgui_rt_core PRIVATE -Wall -Wextra)
endif()

# rimgui_rt: the realtime rendering research kit on OpenGL 3.3 to 4.6. See include/rimgui/rt/lab.h.
add_library(rimgui_rt STATIC
	include/rimgui/rt/environment.h
	include/rimgui/rt/gl.h
	include/rimgui/rt/lab.h
	include/rimgui/rt/mesh.h
	include/rimgui/rt/model.h
	include/rimgui/rt/scene.h
	include/rimgui/rt/shader.h
	include/rimgui/rt/target.h
	include/rimgui/rt/texture.h
	include/rimgui/rt/timer.h
	src/rt/environment.cpp
	src/rt/gl.cpp
	src/rt/lab.cpp
	src/rt/mesh.cpp
	src/rt/model.cpp
	src/rt/scene.cpp
	src/rt/shader.cpp
	src/rt/target.cpp
	src/rt/texture.cpp
	src/rt/timer.cpp
)
target_link_libraries(rimgui_rt PUBLIC rimgui_rt_core rimgui_glfw_deps)
# stb_image_write, kept in this repository (third_party/README.md).
target_include_directories(rimgui_rt SYSTEM PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/stb")
# The kit's own shaders (tonemapping, the texture inspector, the sky), read from here while developing.
target_compile_definitions(rimgui_rt PRIVATE RIMGUI_KIT_SHADERS="${CMAKE_CURRENT_LIST_DIR}/shaders")
if(MSVC)
	target_compile_options(rimgui_rt PRIVATE /W4 /permissive- /utf-8)
	target_compile_definitions(rimgui_rt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
	target_compile_options(rimgui_rt PRIVATE -Wall -Wextra)
endif()
set(RIMGUI_KIT_SHADER_DIR "${CMAKE_CURRENT_LIST_DIR}/shaders" CACHE INTERNAL "")

# A research application's shader folder: `RIMGUI_SHADERS` names it in the source tree (edits there reload live), and
# after each build it and the kit's shaders are copied beside the program, so the program also runs from elsewhere.
function(rimgui_add_shaders target folder)
	get_filename_component(_absolute "${folder}" ABSOLUTE)
	target_compile_definitions(${target} PRIVATE RIMGUI_SHADERS="${_absolute}")
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${_absolute}" "$<TARGET_FILE_DIR:${target}>/shaders"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${RIMGUI_KIT_SHADER_DIR}" "$<TARGET_FILE_DIR:${target}>/shaders/rimgui"
		VERBATIM)
endfunction()

# A Windows program with no console window, whose entry point is still main().
function(rimgui_windows_app target)
	if(WIN32)
		set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
		if(MSVC)
			target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
		endif()
	endif()
endfunction()
