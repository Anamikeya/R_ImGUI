#version 330 core
// Depth only; masked materials cut holes in their shadows as they do in themselves.
in vec2 v_uv;
uniform int u_maps;
uniform int u_alpha_mode;
uniform float u_alpha, u_alpha_cutoff;
uniform sampler2D u_albedo_map;
void main()
{
    if (u_alpha_mode == 1) {
        float alpha = u_alpha * ((u_maps & 1) != 0 ? texture(u_albedo_map, v_uv).a : 1.0);
        if (alpha < u_alpha_cutoff) discard;
    }
}
