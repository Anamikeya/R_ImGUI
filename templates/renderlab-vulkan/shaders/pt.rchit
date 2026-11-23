#version 460
// The nearest hit: which object, which triangle, where on it. The ray generation shader shades it.
#include "pt_common.glsl"
layout(location = 0) rayPayloadInEXT Hit hit;
hitAttributeEXT vec2 bary;
void main()
{
    hit.t = gl_HitTEXT;
    hit.object = gl_InstanceCustomIndexEXT;
    hit.primitive = gl_PrimitiveID;
    hit.bary = bary;
}
