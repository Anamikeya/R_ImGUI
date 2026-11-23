#version 330 core
#include "rimgui_cube.glsl"
// An equirectangular (latitude-longitude) image onto one cubemap face.
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_equirect;

void main()
{
    vec3 d = cube_direction(v_uv);
    vec2 uv = vec2(atan(d.z, d.x) / 6.28318530718 + 0.5, acos(clamp(d.y, -1.0, 1.0)) / 3.14159265359);
    // The level by the screen's own derivatives is wrong at the seam where u wraps; the top level everywhere.
    o_color = vec4(textureLod(u_equirect, uv, 0.0).rgb, 1.0);
}
