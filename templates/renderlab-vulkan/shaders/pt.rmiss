#version 460
// A path ray that left the scene: the ray generation shader adds the sky's light.
#include "pt_common.glsl"
layout(location = 0) rayPayloadInEXT Hit hit;
void main()
{
    hit.t = -1.0;
}
