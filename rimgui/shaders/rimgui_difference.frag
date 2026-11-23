#version 330 core
// A/B difference: |A - B| of the two techniques' HDR outputs after exposure, scaled up and shown in false colour
// (black: the same; blue, green, yellow, red: more and more different).
in vec2 v_uv;
out vec4 o_color;
uniform sampler2D u_a, u_b;
uniform float u_exposure, u_scale;

vec3 heat(float t)
{
    t = clamp(t, 0.0, 1.0);
    vec3 c = mix(vec3(0.0), vec3(0.1, 0.2, 0.9), smoothstep(0.0, 0.25, t));
    c = mix(c, vec3(0.1, 0.8, 0.3), smoothstep(0.25, 0.5, t));
    c = mix(c, vec3(1.0, 0.9, 0.1), smoothstep(0.5, 0.75, t));
    return mix(c, vec3(1.0, 0.15, 0.1), smoothstep(0.75, 1.0, t));
}

void main()
{
    vec3 a = texture(u_a, v_uv).rgb * u_exposure, b = texture(u_b, v_uv).rgb * u_exposure;
    // Compared after a simple tone curve, so bright and dark areas weigh as the eye sees them.
    vec3 d = abs(a / (1.0 + a) - b / (1.0 + b));
    o_color = vec4(heat(max(d.r, max(d.g, d.b)) * u_scale), 1.0);
}
