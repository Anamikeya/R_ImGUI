#version 460
// A shadow ray that reached the light.
#include "pt_common.glsl"
layout(location = 1) rayPayloadInEXT bool shadowed;
void main()
{
    shadowed = false;
}
