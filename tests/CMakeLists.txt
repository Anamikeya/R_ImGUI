# The research kit's tests, and RenderLab run end to end.
#   rimgui_rt_tests                 the kit's parts against answers known exactly
#   renderlab.*                     RenderLab drawing 90 frames in a hidden window with --check: it fails on any
#                                   logged error (shader, OpenGL, a technique that threw) or a flat picture
add_executable(rimgui_rt_tests rimgui_rt_tests.cpp)
target_link_libraries(rimgui_rt_tests PRIVATE rimgui_rt)
target_include_directories(rimgui_rt_tests SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../rimgui/third_party/stb")
target_compile_definitions(rimgui_rt_tests PRIVATE RIMGUI_KIT_SHADERS="${RIMGUI_KIT_SHADER_DIR}")
if(MSVC)
	target_compile_options(rimgui_rt_tests PRIVATE /W4 /permissive- /utf-8)
	target_compile_definitions(rimgui_rt_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_test(NAME rimgui_rt_tests COMMAND rimgui_rt_tests)

set(_fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
add_test(NAME renderlab.fixtures COMMAND rimgui_rt_tests --fixtures "${_fixtures}")
set_tests_properties(renderlab.fixtures PROPERTIES FIXTURES_SETUP renderlab_files)

function(renderlab_check name)
	add_test(NAME renderlab.${name} COMMAND RenderLab --check --hidden --no-settings --log "${CMAKE_CURRENT_BINARY_DIR}/renderlab.${name}.log" ${ARGN})
	set_tests_properties(renderlab.${name} PROPERTIES FIXTURES_REQUIRED renderlab_files TIMEOUT 120)
endfunction()
renderlab_check(forward_pbr --technique "Forward PBR")
renderlab_check(blinn_phong --technique "Forward Blinn-Phong")
renderlab_check(deferred_ssao --technique "Deferred + SSAO")
renderlab_check(compare_split --technique "Forward PBR" --compare "Deferred + SSAO")
renderlab_check(compare_difference --technique "Forward PBR" --compare "Forward Blinn-Phong" --difference)
renderlab_check(environment_pbr --technique "Forward PBR" --environment "${_fixtures}/test_sky.hdr")
renderlab_check(environment_deferred --technique "Deferred + SSAO" --environment "${_fixtures}/test_sky.hdr")
renderlab_check(fixed_resolution --technique "Deferred + SSAO" --resolution 1920x1080 --show "SSAO blurred")

# The Vulkan kit's tests: on the real device, with the validation layers on; any validation message fails them.
if(RIMGUI_HAS_VULKAN)
	add_executable(rimgui_vk_tests rimgui_vk_tests.cpp)
	target_link_libraries(rimgui_vk_tests PRIVATE rimgui_rt_vk)
	target_compile_definitions(rimgui_vk_tests PRIVATE RIMGUI_KIT_SHADERS="${RIMGUI_KIT_SHADER_DIR}")
	rimgui_vulkan_app(rimgui_vk_tests)
	if(MSVC)
		target_compile_options(rimgui_vk_tests PRIVATE /W4 /permissive- /utf-8)
	endif()
	add_test(NAME rimgui_vk_tests COMMAND rimgui_vk_tests --vulkan --hidden --validation)
	set_tests_properties(rimgui_vk_tests PROPERTIES TIMEOUT 120)

	# RenderLabVulkan under the validation layers: any message from them is a logged error, so the check fails.
	function(renderlab_vk_check name)
		add_test(NAME renderlab_vk.${name} COMMAND RenderLabVulkan --check --hidden --validation --no-settings
			--log "${CMAKE_CURRENT_BINARY_DIR}/renderlab_vk.${name}.log" ${ARGN})
		set_tests_properties(renderlab_vk.${name} PROPERTIES FIXTURES_REQUIRED renderlab_files TIMEOUT 120)
	endfunction()
	renderlab_vk_check(forward_pbr --technique "Forward PBR")
	renderlab_vk_check(blinn_phong --technique "Forward Blinn-Phong")
	renderlab_vk_check(deferred_ssao --technique "Deferred + SSAO")
	renderlab_vk_check(sky --technique "Sky and environment")
	renderlab_vk_check(compare_split --technique "Forward PBR" --compare "Deferred + SSAO")
	renderlab_vk_check(compare_difference --technique "Forward PBR" --compare "Forward Blinn-Phong" --difference)
	renderlab_vk_check(environment_pbr --technique "Forward PBR" --environment "${_fixtures}/test_sky.hdr")
	renderlab_vk_check(environment_deferred --technique "Deferred + SSAO" --environment "${_fixtures}/test_sky.hdr")
	renderlab_vk_check(fixed_resolution --technique "Deferred + SSAO" --resolution 1920x1080 --show "SSAO blurred")
	renderlab_vk_check(inspect_output --technique "Sky and environment" --show "Output (HDR)")
	# Ray tracing (on GPUs without it the techniques say so and draw the sky, which still passes).
	renderlab_vk_check(hybrid_rt --technique "Hybrid RT shadows + AO")
	renderlab_vk_check(hybrid_rt_environment --technique "Hybrid RT shadows + AO" --environment "${_fixtures}/test_sky.hdr")
	renderlab_vk_check(path_traced --technique "Path-traced reference")
	renderlab_vk_check(path_traced_environment --technique "Path-traced reference" --environment "${_fixtures}/test_sky.hdr")
	renderlab_vk_check(path_traced_vs_raster --technique "Path-traced reference" --compare "Forward PBR" --difference)
	# The path tracer converges: its picture after 256 frames is close to the one after 1024.
	set(_pt "${CMAKE_CURRENT_BINARY_DIR}/convergence")
	foreach(_frames 256 1024)
		add_test(NAME convergence.path_traced.${_frames} COMMAND RenderLabVulkan --hidden --validation --check --no-settings --resolution 1280x720
			--technique "Path-traced reference" --frames ${_frames} --save-view "${_pt}.${_frames}.png")
		set_tests_properties(convergence.path_traced.${_frames} PROPERTIES FIXTURES_SETUP convergence TIMEOUT 300)
	endforeach()
	add_test(NAME convergence.path_traced COMMAND rimgui_rt_tests --compare "${_pt}.256.png" "${_pt}.1024.png" 0.75 0.005)
	set_tests_properties(convergence.path_traced PROPERTIES FIXTURES_REQUIRED convergence)

	# The two labs agree: the same technique, view and size drawn by RenderLab (OpenGL) and RenderLabVulkan, compared
	# pixel by pixel. Different techniques differ by over a level on average; the same one by far less.
	function(renderlab_agreement name technique)
		set(_gl "${CMAKE_CURRENT_BINARY_DIR}/agreement.${name}.opengl.png")
		set(_vk "${CMAKE_CURRENT_BINARY_DIR}/agreement.${name}.vulkan.png")
		add_test(NAME agreement.${name}.opengl COMMAND RenderLab --hidden --no-settings --resolution 1280x720 --technique "${technique}"
			--save-view "${_gl}" ${ARGN})
		add_test(NAME agreement.${name}.vulkan COMMAND RenderLabVulkan --hidden --validation --check --no-settings --resolution 1280x720
			--technique "${technique}" --save-view "${_vk}" ${ARGN})
		set_tests_properties(agreement.${name}.opengl agreement.${name}.vulkan PROPERTIES
			FIXTURES_SETUP agreement_${name} FIXTURES_REQUIRED renderlab_files TIMEOUT 120)
		add_test(NAME agreement.${name} COMMAND rimgui_rt_tests --compare "${_gl}" "${_vk}" 0.25 0.001)
		set_tests_properties(agreement.${name} PROPERTIES FIXTURES_REQUIRED agreement_${name})
	endfunction()
	renderlab_agreement(forward_pbr "Forward PBR")
	renderlab_agreement(blinn_phong "Forward Blinn-Phong")
	renderlab_agreement(deferred_ssao "Deferred + SSAO")
	renderlab_agreement(environment_pbr "Forward PBR" --environment "${_fixtures}/test_sky.hdr")
endif()
