#include <rimgui/theme.h>
#include <rimgui/widgets.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>

namespace rimgui {

bool begin_toolbar(const char *title, bool *open)
{
	ImGuiWindowClass strip;
	strip.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoTabBar;
	ImGui::SetNextWindowClass(&strip);
	const bool visible = ImGui::Begin(title, open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (visible) {
		ImGui::AlignTextToFramePadding();
	}
	return visible;
}

void end_toolbar()
{
	ImGui::End();
}

void divider()
{
	ImGui::SameLine(0, px(10));
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine(0, px(10));
}

bool button(const char *label, Tone tone, bool enabled, const char *tooltip, ImVec2 size)
{
	const Palette &p = palette();
	int pushed = 0;
	if (enabled && tone != Tone::Normal) {
		const ImVec4 base = tone == Tone::Go ? p.go : tone == Tone::Stop ? p.stop : p.accent_soft;
		const ImVec4 hot = tone == Tone::Go ? p.go_hot : tone == Tone::Stop ? p.stop_hot : p.accent;
		ImGui::PushStyleColor(ImGuiCol_Button, base);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hot);
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, hot);
		pushed = 3;
	}
	ImGui::BeginDisabled(!enabled);
	const bool pressed = ImGui::Button(label, size);
	ImGui::EndDisabled();
	ImGui::PopStyleColor(pushed);
	if (tooltip && tooltip[0]) {
		ImGui::SetItemTooltip("%s", tooltip);
	}
	return pressed && enabled;
}

bool row_toggle(const char *id, bool on, const char *tooltip, float right_offset)
{
	const float size = ImGui::GetFrameHeight() * 0.8f;
	ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - right_offset);
	const ImVec2 at = ImGui::GetCursorScreenPos();
	// The row's text ends here: cover it, fading in over a few pixels.
	ImDrawList *draw = ImGui::GetWindowDrawList();
	const ImU32 solid = ImGui::GetColorU32(ImGuiCol_WindowBg), clear = solid & ~IM_COL32_A_MASK;
	const float top = at.y - ImGui::GetStyle().FramePadding.y * 0.5f;
	const float bottom = at.y + ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 0.5f;
	draw->AddRectFilledMultiColor({at.x - px(18), top}, {at.x - px(4), bottom}, clear, solid, solid, clear);
	draw->AddRectFilled({at.x - px(4), top}, {at.x + size + px(2), bottom}, solid);
	const bool clicked = ImGui::InvisibleButton(id, {size, ImGui::GetTextLineHeight()});
	if (tooltip && tooltip[0]) {
		ImGui::SetItemTooltip("%s", tooltip);
	}
	const ImU32 color = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255) : on ? IM_COL32(170, 180, 195, 255) : IM_COL32(90, 95, 105, 255);
	const ImVec2 middle{at.x + size / 2, at.y + ImGui::GetTextLineHeight() / 2};
	const float r = px(5);
	draw->AddCircle(middle, r, color, 12, px(1.2f));
	if (on) {
		draw->AddCircleFilled(middle, r * 0.45f, color);
	} else {
		draw->AddLine({middle.x - r, middle.y + r}, {middle.x + r, middle.y - r}, color, px(1.5f));
	}
	return clicked;
}

bool section(const char *title, bool open_at_first)
{
	const Palette &p = palette();
	ImGui::PushStyleColor(ImGuiCol_Header, p.band);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{p.band.x + 0.045f, p.band.y + 0.05f, p.band.z + 0.06f, 1});
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{p.band.x + 0.08f, p.band.y + 0.093f, p.band.z + 0.12f, 1});
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (open_at_first) {
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	const bool open = ImGui::TreeNodeEx(title, flags);
	ImGui::PopStyleColor(3);
	return open;
}

bool begin_properties(const char *id)
{
	if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
		return false;
	}
	ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
	ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
	return true;
}

void end_properties()
{
	ImGui::EndTable();
}

void property(const char *name, const char *tooltip)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(name);
	if (tooltip && tooltip[0]) {
		ImGui::SetItemTooltip("%s", tooltip);
	}
	ImGui::TableSetColumnIndex(1);
	ImGui::SetNextItemWidth(-FLT_MIN);
}

void fit_items_to_labels(float label_width)
{
	ImGui::PushItemWidth(std::max(px(70), ImGui::GetContentRegionAvail().x - px(label_width)));
}

void hint(const char *text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, palette().muted);
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

void text_good(const char *text)
{
	ImGui::TextColored(palette().good, "%s", text);
}

void text_warning(const char *text)
{
	ImGui::TextColored(palette().warning, "%s", text);
}

void text_error(const char *text)
{
	ImGui::TextColored(palette().error, "%s", text);
}

void overlay_caption(ImDrawList *draw, ImVec2 min, ImVec2 max, const char *text)
{
	(void)max;
	const float size = ImGui::GetFontSize() * 0.88f, margin = px(8);
	const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text);
	const ImVec2 at{min.x + margin, max.y - extent.y - margin};
	draw->AddRectFilled({at.x - px(6), at.y - px(3)}, {at.x + extent.x + px(6), at.y + extent.y + px(3)}, IM_COL32(10, 12, 16, 150), px(4));
	draw->AddText(ImGui::GetFont(), size, at, IM_COL32(205, 212, 224, 215), text);
}

bool chip(const char *label, bool *on)
{
	const Palette &p = palette();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {px(7), px(2)});
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(9));
	ImGui::PushStyleColor(ImGuiCol_Button, *on ? p.accent_soft : p.field);
	const bool pressed = ImGui::Button(label);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	if (pressed) {
		*on = !*on;
	}
	return pressed;
}

} // namespace rimgui
