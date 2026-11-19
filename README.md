# R_ImGUI

The look of IdlyEngine's editor for any [Dear ImGui](https://github.com/ocornut/imgui) desktop application, and a kit
for realtime rendering research built on it.

| Template | What it is |
|---|---|
| **starter** | A tool with the editor's look: the theme, fonts, toolbar strip, property panels and buttons |
| **renderlab** | A renderer research application: write techniques, compare them side by side, time every pass, look at every buffer |

![StarterApp, made from the starter template](docs/starter_app.png)

![RenderLab comparing deferred shading with SSAO against forward Blinn-Phong](docs/renderlab_compare.png)

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
    void render(rimgui::rt::Frame &frame) override {
        auto timed = frame.scope("Main");                       // shows in Timings, GPU and CPU
        auto &p = frame.shaders->graphics("my_ao", "mesh.vert", "my_ao.frag");
        if (p.ready()) {
            p.use();
            frame.set_camera(p);                                // u_view_projection and the rest
            frame.scene->set_lighting(p);                       // the sun and sky
            p.set("u_radius", radius_);
            frame.scene->draw(p);                               // every object, with its material
        }
        frame.draw_sky();
        frame.expose_depth("Depth", *frame.output);             // shows in Textures
    }
private:
    float radius_ = 0.5f;
};
```

| Panel | What it does |
|---|---|
| **Viewport** | The technique's HDR picture, tonemapped (ACES, Reinhard, clamp; exposure). Left drag orbits, middle drag pans, the wheel zooms, right drag with WASD flies, F frames the scene |
| **Compare** | A/B: two techniques split by a line you drag, each with its own settings and timings |
| **Technique** | The chosen techniques' settings, from `settings()` |
| **Scene** | Objects and materials (albedo, roughness, metallic, emissive), test shapes, OBJ models, the sun and sky, the camera |
| **Textures** | Every texture a technique exposed this frame, by channel (RGB, R, G, B, A) and range; depth shown in metres; shown in the panel or over the whole view |
| **Timings** | GPU time of each pass from timer queries (read a few frames late, so nothing stalls), CPU time, a graph on hover |
| **Log** | Shader errors with the file and line they are on, also inside `#include`d files; OpenGL debug messages |

What the kit gives a technique:

- **Shaders:** `ShaderLibrary` builds programs from files and rebuilds them when you save. `#include "file"` works. A program that fails keeps its last good version, so the picture stays up while you fix it.
- **Render targets:** `RenderTarget` is a framebuffer with any colour formats (RGBA8 to RGBA32F, R16F, R11G11B10F…) and a sampleable depth texture, resized on demand. `make_texture` makes images for compute shaders.
- **Compute:** `glDispatchCompute`, `glMemoryBarrier`, `glBindImageTexture` and `glTexStorage2D`, with OpenGL 4.3.
- **Meshes:** spheres, cubes, planes and tori; OBJ files with smooth normals made where missing; `draw_fullscreen_triangle()` for full-screen passes.
- **Math:** `vec3`, `mat4`, `perspective`, `orthographic`, `look_at` and `inverse`, laid out as GLSL wants them.
- **Capture:** saves PNGs of the view, both sides when comparing.

RenderLab starts with three techniques to copy from:
- **Forward PBR:** GGX with height-correlated Smith visibility and Schlick Fresnel, a sun shadow map with PCF, and views of diffuse only, specular only, the shadow term and normals.
- **Forward Blinn-Phong.**
- **Deferred + SSAO:** a G-buffer, hemisphere SSAO with a range check, a blur, and a lighting pass.

Their shaders are in `templates/renderlab/shaders/`, with the BRDF in `brdf.glsl`. A row of spheres from smooth to rough sits in the default scene for comparing BRDFs.

Command-line options, for scripted captures:

```text
RenderLab.exe --technique "Deferred + SSAO" --compare "Forward PBR" --screenshot out.ppm --screenshot-frame 120
RenderLab.exe --show "SSAO blurred"
```

## What is in this repository

```text
rimgui/                  the libraries: rimgui (the look) and rimgui_rt (the research kit), with the kit's shaders
templates/starter/       StarterApp
templates/renderlab/     RenderLab: three techniques and their shaders
tools/new_app.ps1        makes a new application from a template
```

Building here builds both libraries and both templates (`build\bin\StarterApp.exe`, `build\bin\RenderLab.exe`).

## Dependencies

Dear ImGui (docking branch) and GLFW, fetched on the first configure at pinned commits, the ones IdlyEngine's editor
uses. The OpenGL loader (glad) and the PNG writer (stb_image_write) come with GLFW. To build without a network, point
at local copies:

```powershell
cmake -B build -DRIMGUI_IMGUI_DIR=D:\libs\imgui -DRIMGUI_GLFW_DIR=D:\libs\glfw
```
