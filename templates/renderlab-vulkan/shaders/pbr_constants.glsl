// Forward PBR's push constants, as ForwardPbr::Constants in main.cpp.
#include "rimgui_vk_scene.glsl"
layout(push_constant, scalar) uniform Constants {
    FrameData frame;
    ObjectData object;
    mat4 light_view_projection;
    uint shadow_map;   // Bindless index of the shadow map's depth.
    int view_mode;     // 0 final, 1 diffuse, 2 specular, 3 shadow, 4 normals, 5 ambient only
    int use_shadows;
    float shadow_bias;
    int pcf_radius;
} pc;
