# RenderLabVulkan's shaders

The techniques' shaders, rebuilt while the lab runs whenever one is saved. GLSL files compile with shaderc and `.hlsl`
files with DXC, both to SPIR-V for Vulkan 1.3. The kit's own shaders (`rimgui_vk_*.glsl`) are found after these, so a
file here with the same name replaces the kit's.

Include what a technique needs:

- `rimgui_vk.glsl`: the shared bindless set: textures, cubemaps and storage images by index, and the samplers.
- `rimgui_vk_scene.glsl`: `FrameData` (the camera, the sun and sky, the environment, every object) and `ObjectData`
  (one object's transform, material and mesh), read by address from the push constants.
- `rimgui_vk_lighting.glsl`: the sun, the sky and image-based lighting.
- `rimgui_vk_material.glsl`: a surface's albedo, roughness, metallic, normal, occlusion and emission.
- `rimgui_brdf.glsl`: the GGX pieces, shared with the OpenGL lab.
