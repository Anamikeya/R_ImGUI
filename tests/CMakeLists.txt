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
