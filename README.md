# R_ImGUI

The look of IdlyEngine's editor for any [Dear ImGui](https://github.com/ocornut/imgui) desktop application, and a kit
for realtime rendering research built on it.

| Template | What it is |
|---|---|
| **starter** | A tool with the editor's look: the theme, fonts, toolbar strip, property panels and buttons |
| **renderlab** | A renderer research application: write techniques, compare them side by side, time every pass, look at every buffer |

![StarterApp, made from the starter template](docs/starter_app.png)

![RenderLab comparing forward PBR against deferred shading with SSAO, lit by an HDR environment](docs/renderlab_compare.png)

## Make a new application

```powershell
.\tools\new_app.ps1 -Name MyTool -Destination D:\Projects
.\tools\new_app.ps1 -Name ShadowLab -Template renderlab -Destination D:\Research
```

Each creates a folder holding its own copy of the libraries (`rimgui\`), which builds without this repository:

```powershell
cd D:\Research\ShadowLab
cmake -B build
cmake --build build --config Release
```

Add `-Build` to configure and build straight away. You need CMake 3.24 or later, a C++20 compiler (Visual Studio 2022 on
Windows) and OpenGL 3.3; RenderLab asks for OpenGL 4.3 (for compute shaders) and works with 3.3 without them.

## The look (`rimgui`)

| | |
|---|---|
| **Colours** | Charcoal panels, one blue for what is selected or active, green to start, red to stop; `rimgui::palette()` for your own drawing |
| **Text** | Segoe UI at 15 px for the interface, Consolas for code (`rimgui::push_code_font()`); ImGui's own font where those are missing |
| **Scale** | Sizes and text follow the monitor's scale; `rimgui::px(12)` is 12 px at 100%, whatever the scale now |
| **Layout** | A dock space split like the editor's: a toolbar strip on top, a list on the left, details on the right, a log below, the main view in the middle |
| **Widgets** | `begin_toolbar` and `divider`, `button` in Go, Stop and Accent tones, `section`, `begin_properties` and `property` rows, `chip` filters, `row_toggle`, `hint`, `text_good`, `text_warning` and `text_error`, `overlay_caption` |
| **App shell** | `rimgui::run(config, frame)`: the window, OpenGL, ImGui with docking, the theme and fonts, `on_start` and `on_exit` for GPU resources, and `--screenshot <file.ppm>` for docs and tests |

```cpp
ImGui::Begin("Details");
if (rimgui::section("Transform") && rimgui::begin_properties("##transform")) {
    rimgui::property("Position", "Where it is, in metres");
    ImGui::DragFloat3("##position", position, 0.05f);
    rimgui::end_properties();
}
ImGui::End();
```

## The research kit (`rimgui_rt`) and RenderLab

A technique is a class with a name, its settings and a `render()`; the lab gives it everything else.

```cpp
class MyAO : public rimgui::rt::Technique {
public:
    std::string name() const override { return "My AO"; }
    void settings() override { ImGui::SliderFloat("Radius", &radius_, 0.05f, 2); }
    void serialize(rimgui::rt::Settings &s) override { s.value("radius", radius_); }   // kept between runs
    void render(rimgui::rt::Frame &frame) override {
        auto timed = frame.scope("Main");                       // shows in Timings, GPU and CPU
        auto &p = frame.shaders->graphics("my_ao", "mesh.vert", "my_ao.frag");
        if (p.ready()) {
            p.use();
            frame.set_camera(p);                                // u_view_projection and the rest
            frame.set_lighting(p);                              // the sun, the sky, the HDR environment
            p.set("u_radius", radius_);
            frame.scene->draw(p);                               // every object, its material and its maps
        }
        frame.draw_sky();
        frame.expose_depth("Depth", *frame.output);             // shows in Textures
    }
private:
    float radius_ = 0.5f;
};
```

In the fragment shader, the kit's includes do the common work:

```glsl
#include "rimgui_material.glsl"   // Surface s = surface(v_world, v_normal, v_uv): maps, normal mapping, alpha mask
#include "rimgui_lighting.glsl"   // the sun, ambient_light(...): image-based lighting with the split-sum BRDF table
```

| Panel | What it does |
|---|---|
| **Viewport** | The technique's HDR picture, tonemapped (ACES, Reinhard, clamp; exposure). NaN and infinite pixels show magenta. At the view's size times a render scale (0.25x to 2x), or a fixed 720p to 4K shown letterboxed. Left drag orbits, middle drag pans, the wheel zooms, right drag with WASD flies, F frames the scene |
| **Compare** | A/B: two techniques split by a line you drag, or the difference between them in false colour, scaled up to 100x |
| **Technique** | The chosen techniques' settings; a technique that throws stops with its message and a Retry button, and the lab goes on |
| **Scene** | Objects and their materials and maps (each map can be switched off), glTF and OBJ models, the HDR environment (intensity, rotation, shown as the sky or not), the sun and sky, the camera (with its view as a command-line option to copy) |
| **Textures** | Every texture a technique exposed this frame, by channel (RGB, R, G, B, A) and range; depth shown in metres; shown in the panel or over the whole view |
| **Timings** | GPU time of each pass from timer queries (read a few frames late, so nothing stalls), CPU time, a graph on hover |
| **Log** | Shader errors with the file and line they are on, also inside `#include`d files; OpenGL errors and debug messages; what a model file had that was skipped |

Drop `.gltf`, `.glb`, `.obj` and `.hdr` files on the window to load them, and a PNG or JPEG to make it the selected object's albedo map. The lab keeps its state (camera, scene, models, environment, every technique's settings) in `RenderLab.settings` beside the program, saved every two seconds when something changes, so a crash loses nothing; delete the file to start afresh.

What the kit gives a technique:

- **Shaders:** `ShaderLibrary` builds programs from files and rebuilds them when you save. `#include "file"` works. A program that fails keeps its last good version, so the picture stays up while you fix it.
- **Materials:** glTF's metal-roughness model: albedo, alpha, roughness, metallic, emissive and their maps (normal, metal-roughness, occlusion, emissive), alpha mask and blend, double-sided. Normal maps need no tangents (the frame comes from screen-space derivatives).
- **Models:** glTF 2.0 (`.gltf` with its files, `.glb`, embedded and data-URI images) and OBJ. A file that needs something the loader does not do (Draco or meshopt compression, WebP or Basis images) is refused by name, never loaded as nothing.
- **Image-based lighting:** an equirectangular `.hdr` becomes a cubemap, irradiance, a GGX-prefiltered mip chain and a BRDF table on the GPU (about 150 ms for a 2K image), in 32-bit float so a photographed sun cannot overflow; `ambient_light()` adds multiple-scattering energy compensation.
- **Render targets:** `RenderTarget` is a framebuffer with any colour formats (RGBA8 to RGBA32F, R16F, R11G11B10F...) and a sampleable depth texture, resized on demand. `make_texture` makes images for compute shaders.
- **Compute:** `glDispatchCompute`, `glMemoryBarrier`, `glBindImageTexture` and `glTexStorage2D`, with OpenGL 4.3; `ShaderLibrary::compute` builds and reloads compute shaders.
- **Progressive techniques:** `frame.accumulation` counts frames since anything changed (camera, scene, environment, size, settings), for path tracers and temporal accumulation; Pause time holds `frame.time` still.
- **Meshes:** spheres, cubes, planes and tori; `draw_fullscreen_triangle()` for full-screen passes.
- **Math:** `vec3`, `mat4`, `perspective`, `orthographic`, `look_at` and `inverse`, laid out as GLSL wants them.
- **Capture:** PNG of the view (both sides when comparing), and the linear HDR output as a Radiance `.hdr` before tonemapping.

RenderLab starts with three techniques to copy from:
- **Forward PBR:** GGX with height-correlated Smith visibility and Schlick Fresnel, a sun shadow map with PCF (alpha-masked casters cut holes), image-based lighting, blended objects back to front, and views of the sun's diffuse and specular, the shadow term, normals and ambient light alone.
- **Forward Blinn-Phong.**
- **Deferred + SSAO:** a G-buffer, hemisphere SSAO with a range check, a blur in a compute shader (or a fragment shader without OpenGL 4.3), a lighting pass, and blended objects drawn forward on top.

Their shaders are in `templates/renderlab/shaders/`; the shared BRDF, lighting and material code is in `rimgui/shaders/rimgui_*.glsl` (copy one into your shaders folder under the same name to change it for your application). A row of spheres from smooth to rough sits in the default scene for comparing BRDFs.

Command-line options, for scripted and reproducible captures:

```text
RenderLab.exe --technique "Deferred + SSAO" --compare "Forward PBR" [--difference]
              --model helmet.glb --environment sky.hdr --scene empty
              --camera 0.9,0.7,-1.8,3.6,8,10 --resolution 1920x1080
              --screenshot out.ppm --screenshot-frame 120
RenderLab.exe --show "SSAO blurred"
RenderLab.exe --check --hidden --log check.log     exit 0: drew a picture with no errors; 3: errors; 4: blank
```

Screenshots and checks start from the defaults and do not save settings, so they come out the same every time.

## Tests

```powershell
ctest --test-dir build
```

- `rimgui_rt_tests` holds the kit to answers known exactly (61 checks):
  - a sky of one brightness gives that brightness back from the irradiance and every prefiltered level;
  - the BRDF table gives F0 untouched for a smooth surface seen head-on;
  - a sky with a blinding sun gives no infinity or NaN;
  - a shader error inside an included file is reported at that file's line, and a broken edit keeps the last good build;
  - every render-target format makes a complete framebuffer;
  - glTF factors, maps, alpha, node transforms and bounds are read;
  - a Draco file is refused by name;
  - settings come back as saved;
  - a compute shader writes every pixel.
- `renderlab.*` runs RenderLab with `--check` in a hidden window for every technique, both compare modes, with an HDR environment, and at a fixed resolution.

## Limits

- **OpenGL 3.3 to 4.3 only.** Vulkan or Direct3D 12 (hardware ray tracing, mesh shaders) would need another backend for the window, ImGui and the kit.
- **Compressed glTF** (Draco, meshopt) and **WebP or Basis textures** are refused with a message: decompress them first.
- **glTF extensions beyond metal-roughness** (transmission, clearcoat, sheen, texture transforms) are named in the Log and left out; skins and animations draw in the bind pose.
- Built and tested on Windows (Visual Studio 2022, an RTX 3080). The code avoids Windows-only calls except for finding the system fonts, but has not been built on Linux or macOS.

## What is in this repository

```text
rimgui/                  the libraries: rimgui (the look) and rimgui_rt (the research kit), with the kit's shaders
templates/starter/       StarterApp
templates/renderlab/     RenderLab: three techniques and their shaders
tests/                   the kit's tests and RenderLab's end-to-end checks
tools/new_app.ps1        makes a new application from a template
```

Building here builds both libraries, both templates and the tests (`build\bin\StarterApp.exe`, `build\bin\RenderLab.exe`).

## Dependencies

Dear ImGui (docking branch) and GLFW, fetched on the first configure at pinned commits, the ones IdlyEngine's editor
uses. The OpenGL loader (glad) comes with GLFW. stb_image, stb_image_write and cgltf are kept in
`rimgui/third_party` (see its README for versions and licences). To build without a network, point at local copies of
ImGui and GLFW:

```powershell
cmake -B build -DRIMGUI_IMGUI_DIR=D:\libs\imgui -DRIMGUI_GLFW_DIR=D:\libs\glfw
```
