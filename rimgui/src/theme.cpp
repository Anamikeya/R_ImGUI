#include <rimgui/theme.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>
#include <string>

namespace rimgui {

namespace {
ImGuiStyle themed_style; // At 100% scale, so that changing monitors scales from it rather than compounding.
bool themed = false;
} // namespace

float px(float length)
{
	return length * ImGui::GetFontSize() / base_font_size;
}

const Palette &palette()
{
	static const Palette p;
	return p;
}

void apply_theme(ImGuiStyle &style)
{
	ImGui::StyleColorsDark(&style);
	style.WindowRounding = 4;
	style.ChildRounding = 4;
	style.FrameRounding = 4;
	style.PopupRounding = 4;
	style.GrabRounding = 3;
	style.TabRounding = 4;
	style.ScrollbarRounding = 6;
	style.WindowPadding = {10, 8};
	style.FramePadding = {7, 5};
	style.ItemSpacing = {8, 6};
	style.ItemInnerSpacing = {6, 4};
	style.ScrollbarSize = 13;
	style.GrabMinSize = 10;
	style.IndentSpacing = 16;
	style.WindowBorderSize = 1;
	style.PopupBorderSize = 1;
	style.FrameBorderSize = 0;
	style.TabBarOverlineSize = 2;
	style.DockingSeparatorSize = 2;
	style.SeparatorTextBorderSize = 1;
	style.WindowTitleAlign = {0, 0.5f};
	// Docked tabs shrink further before the bar scrolls, so four tabs in a narrow dock all stay in sight.
	style.TabMinWidthShrink = 40;

	const Palette &p = palette();
	const auto with_alpha = [](ImVec4 c, float a) {
		c.w = a;
		return c;
	};
	ImVec4 *c = style.Colors;
	c[ImGuiCol_Text] = p.text;
	c[ImGuiCol_TextDisabled] = p.muted;
	c[ImGuiCol_WindowBg] = p.panel;
	c[ImGuiCol_ChildBg] = {0, 0, 0, 0};
	c[ImGuiCol_PopupBg] = with_alpha(p.raised, 0.98f);
	c[ImGuiCol_Border] = p.line;
	c[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
	c[ImGuiCol_FrameBg] = p.field;
	c[ImGuiCol_FrameBgHovered] = p.field_hot;
	c[ImGuiCol_FrameBgActive] = p.field_on;
	c[ImGuiCol_TitleBg] = p.deep;
	c[ImGuiCol_TitleBgActive] = p.deep;
	c[ImGuiCol_TitleBgCollapsed] = p.deep;
	c[ImGuiCol_MenuBarBg] = p.deep;
	c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
	c[ImGuiCol_ScrollbarGrab] = p.field_hot;
	c[ImGuiCol_ScrollbarGrabHovered] = p.field_on;
	c[ImGuiCol_ScrollbarGrabActive] = p.accent_soft;
	c[ImGuiCol_CheckMark] = p.accent_bright;
	c[ImGuiCol_SliderGrab] = p.accent;
	c[ImGuiCol_SliderGrabActive] = p.accent_bright;
	c[ImGuiCol_Button] = p.field;
	c[ImGuiCol_ButtonHovered] = p.field_on;
	c[ImGuiCol_ButtonActive] = p.accent_soft;
	c[ImGuiCol_Header] = with_alpha(p.accent_soft, 0.75f);
	c[ImGuiCol_HeaderHovered] = with_alpha(p.accent_soft, 0.50f);
	c[ImGuiCol_HeaderActive] = p.accent_soft;
	c[ImGuiCol_Separator] = p.line;
	c[ImGuiCol_SeparatorHovered] = p.accent_soft;
	c[ImGuiCol_SeparatorActive] = p.accent;
	c[ImGuiCol_ResizeGrip] = {0, 0, 0, 0};
	c[ImGuiCol_ResizeGripHovered] = p.accent_soft;
	c[ImGuiCol_ResizeGripActive] = p.accent;
	c[ImGuiCol_Tab] = p.deep;
	c[ImGuiCol_TabHovered] = p.field_hot;
	c[ImGuiCol_TabSelected] = p.panel;
	c[ImGuiCol_TabSelectedOverline] = p.accent;
	c[ImGuiCol_TabDimmed] = p.deep;
	c[ImGuiCol_TabDimmedSelected] = p.panel;
	c[ImGuiCol_TabDimmedSelectedOverline] = p.line;
	c[ImGuiCol_DockingPreview] = with_alpha(p.accent, 0.55f);
	c[ImGuiCol_DockingEmptyBg] = p.deep;
	c[ImGuiCol_TableHeaderBg] = p.raised;
	c[ImGuiCol_TableBorderStrong] = p.line;
	c[ImGuiCol_TableBorderLight] = with_alpha(p.line, 0.6f);
	c[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
	c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, 0.025f};
	c[ImGuiCol_TextSelectedBg] = with_alpha(p.accent, 0.40f);
	c[ImGuiCol_DragDropTarget] = p.accent_bright;
	c[ImGuiCol_NavCursor] = p.accent_bright;
	c[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.55f};
	c[ImGuiCol_PlotHistogram] = p.accent;
	c[ImGuiCol_PlotLines] = p.accent_bright;

	themed_style = style;
	themed = true;
}

void load_fonts(float pixel_size)
{
	ImFontAtlas &fonts = *ImGui::GetIO().Fonts;
	const auto from_system_sized = [&](const char *file, float size) -> ImFont * {
#ifdef _WIN32
		wchar_t folder[MAX_PATH] = L"";
		if (!GetWindowsDirectoryW(folder, MAX_PATH)) {
			return nullptr;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / "Fonts" / file;
		std::error_code ignored;
		if (!std::filesystem::exists(path, ignored)) {
			return nullptr;
		}
		ImFontConfig config;
		config.SizePixels = size;
		return fonts.AddFontFromFileTTF(path.string().c_str(), size, &config);
#else
		(void)file;
		(void)size;
		return nullptr;
#endif
	};
	const auto built_in = [&] {
		ImFontConfig config;
		config.SizePixels = pixel_size;
		return pixel_size > base_font_size ? fonts.AddFontDefaultVector(&config) : fonts.AddFontDefaultBitmap(&config);
	};
	if (!from_system_sized("segoeui.ttf", pixel_size)) {
		built_in();
	}
	if (!from_system_sized("consola.ttf", pixel_size)) {
		built_in();
	}
}

ImFont *code_font()
{
	const auto &fonts = ImGui::GetIO().Fonts->Fonts;
	return fonts.Size > 1 ? fonts[1] : nullptr;
}

void push_code_font()
{
	ImGui::PushFont(code_font(), ImGui::GetFontSize() * 0.9f);
}

void scale_style(float scale, float font_scale)
{
	ImGuiStyle &style = ImGui::GetStyle();
	if (!themed) {
		apply_theme(style);
	}
	style = themed_style;
	style.ScaleAllSizes(scale);
	style.FontScaleDpi = font_scale > 0 ? scale / font_scale : 1;
}

} // namespace rimgui
