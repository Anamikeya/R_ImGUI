# Vulkan for RenderLab: plan

**Status: decided 2026-11-23, in progress.**
- A separate Vulkan lab that shares the UI, scene, model loading, settings and tests with the OpenGL lab.
- Every feature asked for: a path-traced reference, ray-traced shadows and AO, mesh shaders, and HLSL as well as GLSL.
- Shader compilers and libraries come from the Vulkan SDK.
- The OpenGL lab stays.

## What there is to build on

- **On this PC:**
  - Vulkan SDK 1.3.236: shaderc, glslang, DXC, VMA 3.0.1, Volk, the validation layers.
  - Vulkan 1.4 on an RTX 3080, which has hardware ray tracing and mesh shaders.
- **Dear ImGui:** at the pinned commit it has a Vulkan backend with dynamic rendering.
- **The OpenGL kit:**
  - its CPU work: math, camera, settings, glTF/OBJ parsing, HDR decoding, materials and scene description;
  - its lab UI: Viewport, Compare, Technique, Scene, Textures, Timings, Log, drag and drop, saved state, `--check`.
  - Both are tied to OpenGL today, and are worth sharing rather than writing twice.

## Design

```text
rimgui/            the look (unchanged)
rimgui/core        NEW  shared, no graphics API: math, camera, settings, model files to CPU data, HDR images,
                        the scene description, the lab's UI (LabCore) over a backend interface
rimgui/rt          OpenGL kit: uploads core data, the OpenGL backend for LabCore (behaviour unchanged)
rimgui/vk          NEW  Vulkan kit and the Vulkan backend for LabCore
templates/renderlab         OpenGL RenderLab (unchanged for its users)
templates/renderlab-vulkan  NEW  Vulkan RenderLab with the techniques below
```

- **Vulkan baseline:** 1.3 core, with dynamic rendering, synchronization2, descriptor indexing (bindless textures) and
  buffer device addresses.
- **Optional features**, each switched on only when the GPU has it, and labelled in the UI when it does not:
  - `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline` and `VK_KHR_ray_query`;
  - `VK_EXT_mesh_shader`.
- **The Vulkan kit gives a technique** what the OpenGL kit does:
  - render targets of any format, buffers and images through VMA;
  - pipeline builders for graphics, compute, ray tracing and mesh pipelines;
  - shaders that rebuild when saved (GLSL through shaderc, HLSL through DXC), `#include`, and errors placed by file and
    line;
  - GPU timestamps per pass;
  - barriers by intent ("this image is now read by the fragment shader");
  - the scene uploaded once:
    - one vertex and index buffer;
    - materials in a storage buffer;
    - every texture in one bindless array;
    - acceleration structures for ray tracing;
    - meshlets for mesh shaders.
- **Validation layers** run whenever the lab is checked (`--check --validation`). Any validation message is an error,
  so checks fail on misuse of the API, not only on a blank picture.

## Techniques in RenderLab-Vulkan

| Technique | What it shows |
|---|---|
| Forward PBR | The OpenGL lab's forward PBR (shadow map, IBL, alpha) in Vulkan, to check the two labs agree |
| Deferred + SSAO | G-buffer, compute SSAO and blur, lighting |
| Path-traced reference | Hardware ray tracing, a path tracer with next-event estimation to the sun and the environment, accumulating while nothing changes: the ground truth to A/B raster techniques against |
| Hybrid ray-traced shadows and AO | Raster G-buffer plus ray queries for sun shadows and ambient occlusion, compared with the shadow map and SSAO |
| Mesh-shader pipeline | Meshlets with per-meshlet frustum and normal-cone culling in a task shader; how many were culled |
| HLSL variant | Forward PBR's shaders in HLSL through DXC, showing both languages side by side |

## Slices

| Slice | What | Done when |
|---|---|---|
| 0 | `rimgui::run` on Vulkan: instance, device, swapchain, frames in flight, resize and minimise, ImGui's Vulkan backend, `--screenshot`, validation messages | StarterApp runs with `--vulkan`, a screenshot matches the OpenGL one, no validation messages |
| 1 | `rimgui/core`: the CPU half of the kit moved out of the OpenGL kit | The OpenGL tests still pass, unchanged |
| 2 | `rimgui/vk` basics: device context, VMA, buffers, images, render targets, uploads, bindless descriptors, pipeline builders, shaderc and DXC with hot reload, timestamps | Unit tests on the device: a compute shader writes an image; an error in an included shader is placed at its line; HLSL and GLSL both build |
| 3 | `LabCore`: the lab's UI over a backend; the OpenGL lab ported onto it; the Vulkan backend (tonemap, inspector, difference, capture, IBL on compute, scene upload) | The OpenGL RenderLab checks still pass; the Vulkan lab draws the sky and the IBL tests pass on Vulkan |
| 4 | Forward PBR and Deferred + SSAO in Vulkan | `--check --validation` passes for both; the difference against the OpenGL lab is small |
| 5 | Ray tracing: BLAS/TLAS, the path-traced reference with accumulation, hybrid RT shadows and AO | Checks pass; the path tracer converges (the difference between frame 256 and frame 1024 is small) |
| 6 | Mesh shaders: meshlets, task-shader culling | Checks pass; culled meshlets shown in Timings |
| 7 | HLSL variant; `new_app.ps1 -Template renderlab-vulkan`; README; every check under validation | All tests pass; a new Vulkan app builds outside the repository |

## Risks

| Risk | Plan |
|---|---|
| The SDK is 1.3.236 (December 2022) | Everything used here exists in it, including `VK_EXT_mesh_shader`; newer SDKs work too |
| Two labs drifting apart | `LabCore` holds all the UI, settings and checks once; backends only draw |
| A GPU without ray tracing or mesh shaders | Those techniques say so in the Technique panel and draw nothing; the rest works |
| Machines without the Vulkan SDK | Building the Vulkan lab needs it, and CMake says so plainly; the OpenGL lab builds without it |

## Status

All slices are done (November 2026). Where the result differs from the plan:

- The core is `rimgui_rt_core` plus `rimgui/src/rt/lab_core.inl` (the lab over a backend, as a template) rather than a
  `rimgui/core` folder; the OpenGL lab's public API did not change.
- Barriers are whole-memory (`barrier(cb)`) between passes, with every image in `VK_IMAGE_LAYOUT_GENERAL`, rather than
  barriers by intent.
- The path tracer samples the sun directly; the sky and the environment are reached by its bounces, not by importance
  sampling the environment image.
- `--save-view`, `--frames` and `rimgui_rt_tests --compare` were added to check the labs against each other: the two
  labs' Forward PBR, Blinn-Phong and Deferred + SSAO agree within 0.05 levels on average, HLSL and GLSL Forward PBR and
  the mesh-shader Forward PBR give the same pictures, and the path tracer after 256 frames is within 0.5 levels of 1024.
