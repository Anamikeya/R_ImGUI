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
target_compile_features(rimgui PUBLIC cxx_std_17)
if(MSVC)
	target_compile_options(rimgui PRIVATE /W4 /permissive- /utf-8)
	target_compile_definitions(rimgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
	target_compile_options(rimgui PRIVATE -Wall -Wextra -Wpedantic)
endif()

# A Windows program with no console window, whose entry point is still main().
function(rimgui_windows_app target)
	if(WIN32)
		set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
		if(MSVC)
			target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
		endif()
	endif()
endfunction()
