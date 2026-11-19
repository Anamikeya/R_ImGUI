# R_ImGUI

The look of IdlyEngine's editor for any [Dear ImGui](https://github.com/ocornut/imgui) desktop application: the theme,
the fonts, the toolbar strip, property panels and buttons. Make a new application from the template, and it starts
with all of it.

![StarterApp, made from the template](docs/starter_app.png)

## Make a new application

```powershell
.\tools\new_app.ps1 -Name MyTool -Destination D:\Projects
```

This creates `D:\Projects\MyTool`. It holds its own copy of the look (`rimgui\`) and builds without this repository:

```powershell
cd D:\Projects\MyTool
cmake -B build
cmake --build build --config Release
```

Add `-Build` to the first command to configure and build the new application straight away. Its panels are in
`src\main.cpp`.

You need CMake 3.24 or later, a C++20 compiler (Visual Studio 2022 on Windows) and OpenGL 3.3.

## What it gives you

| | |
|---|---|
| **Colours** | Charcoal panels, one blue for what is selected or active, green to start, red to stop; `rimgui::palette()` for your own drawing |
| **Text** | Segoe UI at 15 px for the interface, Consolas for code (`rimgui::push_code_font()`); ImGui's own font where those are missing |
| **Scale** | Sizes and text follow the monitor's scale; `rimgui::px(12)` is 12 px at 100%, whatever the scale now |
| **Layout** | A dock space split like the editor's: a toolbar strip on top, a list on the left, details on the right, a log below, the main view in the middle |
| **Widgets** | `begin_toolbar` and `divider`, `button` in Go, Stop and Accent tones, `section`, `begin_properties` and `property` rows, `chip` filters, `row_toggle`, `hint`, `text_good`, `text_warning` and `text_error`, `overlay_caption` |
| **App shell** | `rimgui::run(config, frame)`: the window, OpenGL, ImGui with docking, the theme and fonts, and `--screenshot <file.ppm>` for docs and tests |

A panel in the house style:

```cpp
ImGui::Begin("Details");
if (rimgui::section("Transform") && rimgui::begin_properties("##transform")) {
    rimgui::property("Position", "Where it is, in metres");
    ImGui::DragFloat3("##position", position, 0.05f);
    rimgui::end_properties();
}
ImGui::End();

if (rimgui::begin_toolbar("Toolbar")) {
    if (rimgui::button("Run", rimgui::Tone::Go)) start();
    ImGui::SameLine();
    if (rimgui::button("Stop", rimgui::Tone::Stop, running)) stop();
}
rimgui::end_toolbar();
```

## What is in this repository

```text
rimgui/      the library: theme, fonts, widgets and the application shell (copied into each new application)
template/    StarterApp, what a new application starts as
tools/       new_app.ps1
```

Building here builds the library and StarterApp (`build\bin\StarterApp.exe`).

## Dependencies

Dear ImGui (docking branch) and GLFW, fetched on the first configure at pinned commits, the ones IdlyEngine's editor
uses. To build without a network, point at local copies:

```powershell
cmake -B build -DRIMGUI_IMGUI_DIR=D:\libs\imgui -DRIMGUI_GLFW_DIR=D:\libs\glfw
```
