#include "QuartzChrome.h"
#include "QuartzLogView.h"
#include "QuartzNotify.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace QuartzUI
{
namespace
{
	FBackdropBlur GBlur;
	bool         GGlassVisible = false;
	bool         GSettingsLoaded = false;
	bool         GShowAbout = false;
	EAppTheme    GTheme = EAppTheme::DeepDark;
	ImVec4       GTabBorder;   // accent, tab outlines only

	constexpr int32 MaxRecentFiles = 10;
	std::vector<std::string> GRecentFiles;

	// imgui window-backdrop hook. every popup, menu, tooltip and modal gets glass
	void WindowGlassBackdrop(ImGuiWindow* Window, ImDrawList* DrawList, const ImRect& Rect,
		float Rounding, ImDrawFlags RoundingFlags, ImU32 BgCol)
	{
		const ImTextureID Tex = GBlur.Texture();
		if (Tex == 0)
		{
			return;
		}

		const ImGuiWindowFlags Flags = Window->Flags;
		if (Window->DockIsActive || (Flags & ImGuiWindowFlags_DockNodeHost))
		{
			return;
		}

		const bool bTransient = (Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip
			| ImGuiWindowFlags_ChildMenu)) != 0;
		const float BgAlpha = ((BgCol >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
		const bool bFloatingPanel = !(Flags & ImGuiWindowFlags_ChildWindow)
			&& !(Flags & ImGuiWindowFlags_NoTitleBar)
			&& BgAlpha < 0.999f;
		if (!bTransient && !bFloatingPanel)
		{
			return;
		}

		const ImGuiViewport* Viewport = Window->Viewport;
		const ImVec2 Size = Viewport->Size;
		if (Size.x <= 0.0f || Size.y <= 0.0f)
		{
			return;
		}

		const ImVec2 UV0((Rect.Min.x - Viewport->Pos.x) / Size.x,
			(Rect.Min.y - Viewport->Pos.y) / Size.y);
		const ImVec2 UV1((Rect.Max.x - Viewport->Pos.x) / Size.x,
			(Rect.Max.y - Viewport->Pos.y) / Size.y);

		UiDropShadow(DrawList, Rect.Min, Rect.Max, Rounding);
		DrawList->AddImageRounded(Tex, Rect.Min, Rect.Max, UV0, UV1, IM_COL32_WHITE, Rounding, RoundingFlags);
		GGlassVisible = true;

		// a floated panel has nothing under its titlebar but glass
		if (bFloatingPanel)
		{
			const float TitleBarBottom = Window->Pos.y + Window->TitleBarHeight;
			const float Thickness = ImMax(1.0f, floorf(UiDpi()));
			const float Y = TitleBarBottom - Thickness * 0.5f;
			DrawList->AddLine(ImVec2(Rect.Min.x, Y), ImVec2(Rect.Max.x, Y),
				ImGui::GetColorU32(ImGuiCol_Border), Thickness);
		}
	}

	void DrawInactiveTabOutlines()
	{
		ImGuiContext& Context = *ImGui::GetCurrentContext();
		ImGuiStyle& Style = ImGui::GetStyle();
		const float Thickness = ImMax(1.0f, floorf(UiDpi()));

		ImVec4 Edge = GTabBorder;
		Edge.w = Tune().TabBorderAlpha;
		const ImU32 Col = ImGui::GetColorU32(Edge);
		for (ImGuiWindow* Window : Context.Windows)
		{
			if (!Window->DockIsActive || Window->DockTabIsVisible)
			{
				continue;
			}
			const ImRect& Rect = Window->DC.DockTabItemRect;
			const float Width = Rect.GetWidth();
			if (Width <= 0.0f || Rect.GetHeight() <= 0.0f)
			{
				continue;
			}

			const float Rounding = ImMax(0.0f, ImMin(Style.TabRounding, Width * 0.5f - 1.0f));
			const float Y1 = Rect.Min.y + 1.0f;
			const float Y2 = Rect.Max.y - Style.TabBarBorderSize;
			// host layer, not foreground. foreground would punch these edges through the glass
			ImDrawList* DrawList = Window->DockNode->HostWindow->DrawList;
			DrawList->PathLineTo(ImVec2(Rect.Min.x + 0.5f, Y2));
			DrawList->PathArcToFast(ImVec2(Rect.Min.x + Rounding + 0.5f, Y1 + Rounding + 0.5f), Rounding, 6, 9);
			DrawList->PathArcToFast(ImVec2(Rect.Max.x - Rounding - 0.5f, Y1 + Rounding + 0.5f), Rounding, 9, 12);
			DrawList->PathLineTo(ImVec2(Rect.Max.x - 0.5f, Y2));
			DrawList->PathStroke(Col, 0, Thickness);
		}
	}

	enum class EBarEdge : uint8
	{
		None = 0,
		Top = 1 << 0,
		Bottom = 1 << 1,
	};
	constexpr EBarEdge operator|(EBarEdge Lhs, EBarEdge Rhs)
	{
		return (EBarEdge)((uint8)Lhs | (uint8)Rhs);
	}
	constexpr bool EnumHasAnyFlags(EBarEdge Value, EBarEdge Flags)
	{
		return ((uint8)Value & (uint8)Flags) != 0;
	}

	// imgui's window border is all-or-nothing. sides on a full-width bar read as a doubled edge
	void DrawBarEdges(EBarEdge Edges)
	{
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		const ImVec2 Min = ImGui::GetWindowPos();
		const ImVec2 Max(Min.x + ImGui::GetWindowWidth(), Min.y + ImGui::GetWindowHeight());
		const ImU32 Col = ImGui::GetColorU32(ImGuiCol_Border);
		const float Thickness = ImMax(1.0f, floorf(UiDpi()));
		const float Half = Thickness * 0.5f;
		if (EnumHasAnyFlags(Edges, EBarEdge::Top))
		{
			DrawList->AddLine(ImVec2(Min.x, Min.y + Half), ImVec2(Max.x, Min.y + Half), Col, Thickness);
		}
		if (EnumHasAnyFlags(Edges, EBarEdge::Bottom))
		{
			DrawList->AddLine(ImVec2(Min.x, Max.y - Half), ImVec2(Max.x, Max.y - Half), Col, Thickness);
		}
	}

	float StatusCellPadding()
	{
		return ImGui::GetFontSize() * Tune().StatusPadEm;
	}

	// white or black, whichever the fill can carry. rec. 601 luma keeps amber dark-on-light
	ImU32 ContrastText(ImU32 Fill)
	{
		const float R = ((Fill >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
		const float G = ((Fill >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
		const float B = ((Fill >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
		return (0.299f * R + 0.587f * G + 0.114f * B) > 0.55f
			? IM_COL32(16, 18, 20, 255) : IM_COL32(255, 255, 255, 255);
	}

	bool StatusCell(const char* Id, const char* Text, ImU32 TextColor,
		bool bDivider, const char* Tooltip, ImU32 ChipFill = 0)
	{
		const float Pad = StatusCellPadding();
		const float Width = ImGui::CalcTextSize(Text).x + Pad * 2.0f;
		const float Height = ImGui::GetWindowHeight();
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Width, Min.y + Height);

		ImGui::PushID(Id);
		ImGui::InvisibleButton("##cell", ImVec2(Width, Height));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bClicked = ImGui::IsItemClicked();
		ImGui::PopID();

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		if (ChipFill)
		{
			DrawList->AddRectFilled(Min, Max, ChipFill);
			TextColor = ContrastText(ChipFill);
		}
		const float Hot = UiAnimStr(Id, bHovered ? 1.0f : 0.0f, Tune().MenuSpeed);
		if (Hot > 0.005f)
		{
			DrawList->AddRectFilled(Min, Max, IM_COL32(255, 255, 255, (int32)(24 * Hot)));
		}
		if (bDivider && !ChipFill)
		{
			DrawList->AddLine(ImVec2(Min.x, Min.y + Height * 0.3f), ImVec2(Min.x, Max.y - Height * 0.3f),
				IM_COL32(255, 255, 255, 34));
		}
		DrawList->AddText(ImVec2(Min.x + Pad, Min.y + (Height - ImGui::GetTextLineHeight()) * 0.5f), TextColor, Text);

		if (bHovered && Tooltip)
		{
			ImGui::SetTooltip("%s", Tooltip);
		}
		ImGui::SameLine(0, 0);
		return bClicked;
	}

	float StatusCellWidth(const char* Text)
	{
		return ImGui::CalcTextSize(Text).x + StatusCellPadding() * 2.0f;
	}

	void DrawAboutWindow()
	{
		if (!GShowAbout)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(420.0f * UiDpi(), 0.0f), ImGuiCond_Appearing);
		if (ImGui::Begin("About Quartz", &GShowAbout,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline), "Quartz");
			ImGui::TextDisabled("by XZCendence");
			ImGui::TextDisabled("an actually usable log viewer for UEFN and anything else");
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			ImGui::TextDisabled("Dear ImGui %s  --  JetBrains Mono (OFL)", IMGUI_VERSION);
		}
		ImGui::End();
	}
}

bool ChromeSettingsWereLoaded()
{
	return GSettingsLoaded;
}

void NoteRecentFile(const char* Path)
{
	if (!Path || !Path[0])
	{
		return;
	}
	for (size_t Index = 0; Index < GRecentFiles.size(); ++Index)
	{
		if (GRecentFiles[Index] == Path)
		{
			GRecentFiles.erase(GRecentFiles.begin() + Index);
			break;
		}
	}
	GRecentFiles.insert(GRecentFiles.begin(), Path);
	if ((int32)GRecentFiles.size() > MaxRecentFiles)
	{
		GRecentFiles.resize(MaxRecentFiles);
	}
	ImGui::MarkIniSettingsDirty();
}

void RegisterChromeSettingsHandler()
{
	ImGuiSettingsHandler Handler;
	Handler.TypeName = "QuartzChrome";
	Handler.TypeHash = ImHashStr("QuartzChrome");
	Handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void*
	{
		GSettingsLoaded = true;
		return (void*)1;
	};
	Handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* Line)
	{
		int32 Value = 0;
		if (strncmp(Line, "Theme=", 6) == 0)
		{
			// stored by name so the list can grow without renumbering a saved choice
			EAppTheme Parsed;
			if (AppThemeFromName(Line + 6, Parsed))
			{
				GTheme = Parsed;
			}
		}
		else if (sscanf(Line, "Notify=%d", &Value) == 1)
		{
			QuartzNotify::Enabled() = Value != 0;
		}
		else if (strncmp(Line, "Recent=", 7) == 0)
		{
			// kept in written order. File= lines below bump reopened files to the front
			bool bKnown = false;
			for (const std::string& Known : GRecentFiles)
			{
				if (Known == Line + 7)
				{
					bKnown = true;
					break;
				}
			}
			if (!bKnown && (int32)GRecentFiles.size() < MaxRecentFiles)
			{
				GRecentFiles.push_back(Line + 7);
			}
		}
		else if (strncmp(Line, "File=", 5) == 0)
		{
			HostOpenFile(Line + 5);
		}
	};
	Handler.ApplyAllFn = [](ImGuiContext*, ImGuiSettingsHandler*)
	{
		ApplyTheme(UiDpi());
	};
	Handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* H, ImGuiTextBuffer* Buf)
	{
		Buf->appendf("[%s][State]\n", H->TypeName);
		Buf->appendf("Theme=%s\n", AppThemeName(GTheme));
		Buf->appendf("Notify=%d\n", QuartzNotify::Enabled() ? 1 : 0);
		// recent before file. on load, HostOpenFile bumps each restored file to the front
		for (const std::string& Recent : GRecentFiles)
		{
			Buf->appendf("Recent=%s\n", Recent.c_str());
		}
		for (int32 Index = 0; Index < GetOpenFileCount(); ++Index)
		{
			Buf->appendf("File=%s\n", GetOpenFilePath(Index));
		}
		Buf->append("\n");
	};
	ImGui::AddSettingsHandler(&Handler);
}

void ApplyTheme(float Dpi)
{
	UiDpi() = Dpi;
	ApplyAppTheme(GTheme);

	ImGuiStyle& Style = ImGui::GetStyle();
	Style.WindowRounding = 5.0f;
	Style.ChildRounding = 3.0f;
	Style.PopupRounding = 5.0f;
	Style.FrameRounding = 3.0f;
	Style.ScrollbarRounding = 4.0f;
	Style.GrabRounding = 3.0f;
	Style.TabRounding = 4.0f;

	// tight radius on menu rows. MenuItemRounding, not SelectableRounding
	// menuitemex/beginmenuex swap the latter out while drawing
	Style.SelectableRounding = 2.0f;
	Style.MenuItemRounding = 2.0f;
	const ImVec4 RowAccent = Style.Colors[ImGuiCol_TabSelectedOverline];
	const ImVec4 RowFill(RowAccent.x * 0.34f, RowAccent.y * 0.34f, RowAccent.z * 0.34f, 1.0f);
	Style.Colors[ImGuiCol_Header] = RowFill;
	Style.Colors[ImGuiCol_HeaderHovered] = RowFill;
	Style.Colors[ImGuiCol_HeaderActive] =
		ImVec4(RowAccent.x * 0.48f, RowAccent.y * 0.48f, RowAccent.z * 0.48f, 1.0f);

	// we stroke the tab edge ourselves. imgui's outline would wash the accent out
	Style.TabBorderSize = 0.0f;
	Style.TabBarBorderSize = 1.0f;
	Style.TabBarOverlineSize = 0.0f;
	Style.TabCloseButtonMinWidthSelected = -1.0f;
	Style.TabCloseButtonMinWidthUnselected = -1.0f;

	// one hover colour serves both selected and unselected tabs upstream
	ImVec4& TabHover = Style.Colors[ImGuiCol_TabHovered];
	const ImVec4 TabSelected = Style.Colors[ImGuiCol_TabSelected];
	const ImVec4 TabAccent = Style.Colors[ImGuiCol_TabSelectedOverline];
	TabHover = ImLerp(TabSelected, TabAccent, 0.18f);
	TabHover.w = 1.0f;

	// idle tab is an outline, not a slab
	Style.Colors[ImGuiCol_Tab].w = 0.0f;
	GTabBorder = ImVec4(TabAccent.x, TabAccent.y, TabAccent.z, 1.0f);

	// accent edge on buttons and frames via SetFrameEdgeColor, not ImGuiCol_Border
	// upstream shares Border with windows and menus, which keep the neutral edge
	Style.FrameBorderSize = 1.0f;
	static ImVec4 FrameEdge;
	FrameEdge = ImVec4(TabAccent.x, TabAccent.y, TabAccent.z, Tune().FrameEdgeAlpha);
	ImGui::SetFrameEdgeColor(&FrameEdge);

	// unfocused dock tabs should look the same as focused ones
	Style.Colors[ImGuiCol_TabDimmed] = Style.Colors[ImGuiCol_Tab];
	Style.Colors[ImGuiCol_TabDimmedSelected] = Style.Colors[ImGuiCol_TabSelected];
	Style.Colors[ImGuiCol_TabDimmedSelectedOverline] = Style.Colors[ImGuiCol_TabSelectedOverline];
	Style.Colors[ImGuiCol_TitleBgActive] = Style.Colors[ImGuiCol_TitleBg];

	Style.ScaleAllSizes(Dpi);
	Style.FontScaleDpi = Dpi;
}

void InitChrome(ID3D11Device* Device, ID3D11DeviceContext* Context)
{
	GBlur.Init(Device, Context);
	GImGuiWindowBackdrop = &WindowGlassBackdrop;
}

void ShutdownChrome()
{
	GImGuiWindowBackdrop = nullptr;
	GBlur.Shutdown();
}

void BeginFrameChrome()
{
	ImGui::GetStyle().TabRounding = ImGui::GetFontSize() * Tune().TabRadiusEm;
	// fill must stay translucent or the blurred backdrop is invisible
	ImGui::GetStyle().Colors[ImGuiCol_PopupBg].w = Tune().GlassTint;
	GGlassVisible = false;
}

void DrawMenuBar()
{
	// border off. DrawBarEdges puts the wanted edges back. pop right after Begin
	// because the outline is stroked during Begin
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const bool bVisible = ImGui::BeginMainMenuBar();
	ImGui::PopStyleVar();
	if (!bVisible)
	{
		return;
	}

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Open log file...", "Ctrl+O"))
		{
			HostOpenFileDialog();
		}
		if (ImGui::BeginMenu("Quick open"))
		{
			const char* EditorPath = GetEditorLogPath();
			const char* GamePath = GetGameLogPath();
			ImGui::BeginDisabled(!EditorPath[0] || !HostFileExists(EditorPath));
			if (ImGui::MenuItem("UEFN Editor Log"))
			{
				HostOpenFile(EditorPath);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && EditorPath[0])
			{
				ImGui::SetTooltip("%s", EditorPath);
			}
			ImGui::BeginDisabled(!GamePath[0] || !HostFileExists(GamePath));
			if (ImGui::MenuItem("Fortnite Client Log"))
			{
				HostOpenFile(GamePath);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && GamePath[0])
			{
				ImGui::SetTooltip("%s", GamePath);
			}
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Recent files", !GRecentFiles.empty()))
		{
			// a click can reorder GRecentFiles. pick the action first, act after the loop
			std::string Clicked;
			for (const std::string& Recent : GRecentFiles)
			{
				if (ImGui::MenuItem(Recent.c_str()))
				{
					Clicked = Recent;
				}
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Clear recently opened"))
			{
				GRecentFiles.clear();
				ImGui::MarkIniSettingsDirty();
			}
			ImGui::EndMenu();
			if (!Clicked.empty())
			{
				HostOpenFile(Clicked.c_str());
			}
		}
		ImGui::TextDisabled("Or drop a file on the window");
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", "Alt+F4"))
		{
			HostRequestExit();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("View"))
	{
		if (ImGui::MenuItem("Notifications", nullptr, QuartzNotify::Enabled()))
		{
			QuartzNotify::Enabled() = !QuartzNotify::Enabled();
			ImGui::MarkIniSettingsDirty();
		}
		if (ImGui::MenuItem("Freshness flash", nullptr, LogViewFreshFlashEnabled()))
		{
			LogViewFreshFlashEnabled() = !LogViewFreshFlashEnabled();
			ImGui::MarkIniSettingsDirty();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Briefly highlight lines as they arrive");
		}
		ImGui::Separator();
		if (ImGui::BeginMenu("Theme"))
		{
			for (int32 Index = 0; Index < (int32)EAppTheme::Count; ++Index)
			{
				if (Index == (int32)EAppTheme::ImGuiDark)
				{
					ImGui::SeparatorText("Built in");
				}
				if (Index == (int32)EAppTheme::DeepDark)
				{
					ImGui::SeparatorText("House");
				}
				if (Index == (int32)EAppTheme::Moonlight)
				{
					ImGui::SeparatorText("Community styles");
				}
				if (Index == (int32)EAppTheme::Dracula)
				{
					ImGui::SeparatorText("Editor palettes");
				}

				const EAppTheme Candidate = (EAppTheme)Index;
				if (ImGui::MenuItem(AppThemeName(Candidate), nullptr, Candidate == GTheme))
				{
					GTheme = Candidate;
					ApplyTheme(UiDpi());
					ImGui::MarkIniSettingsDirty();
				}
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		if (ImGui::MenuItem("Reset layout"))
		{
			for (ImGuiWindow* Window : ImGui::GetCurrentContext()->Windows)
			{
				ImGui::ClearWindowSettings(Window->Name);
			}
			ForgetDockedWindows();
			ImGui::MarkIniSettingsDirty();
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("About Quartz"))
		{
			GShowAbout = true;
		}
		ImGui::EndMenu();
	}
	DrawBarEdges(EBarEdge::Top | EBarEdge::Bottom);
	ImGui::EndMainMenuBar();

	DrawAboutWindow();
}

void DrawStatusBar(const FChromeState& State)
{
	const float Height = ImGui::GetFontSize() * Tune().StatusBarEm;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, RGBAv(Palette::StatusIdle));
	if (ImGui::BeginViewportSideBar("##status-bar", ImGui::GetMainViewport(), ImGuiDir_Down, Height,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav))
	{
		const ImU32 Weak = RGBA(Palette::StatusWeakText);	

		const bool bWatching = State.WatchedFiles > 0;
		const ImU32 WatchChip = RGBA(Palette::ChipIdle);
		const ImU32 WatchColor = ContrastText(WatchChip);
		char StateText[64];
		snprintf(StateText, sizeof(StateText), "%s",
			bWatching ? (State.WatchedFiles == 1 ? "watching 1 file" : "watching files") : "no log open");
		if (bWatching && State.WatchedFiles > 1)
		{
			snprintf(StateText, sizeof(StateText), "watching %d files", State.WatchedFiles);
		}
		StatusCell("watch-state", StateText, WatchColor, false, State.TailStatus, WatchChip);

		// right-aligned. build the list first so the group's width is known
		struct FCell
		{
			const char* Id;
			char        Text[64];
			int32       Tone;
			const char* Tip;
		};
		FCell Cells[6];
		int32 Count = 0;
		auto Add = [&](const char* Id, int32 Tone, const char* Tip, const char* Fmt, ...)
		{
			if (Count >= (int32)(sizeof(Cells) / sizeof(Cells[0])))
			{
				return;
			}
			FCell& Cell = Cells[Count++];
			Cell.Id = Id;
			Cell.Tone = Tone;
			Cell.Tip = Tip;
			va_list Args;
			va_start(Args, Fmt);
			vsnprintf(Cell.Text, sizeof(Cell.Text), Fmt, Args);
			va_end(Args);
		};

		if (State.Build && State.Build[0])
		{
			Add("build", State.BuildTone, "Last Verse compile result seen in the log", "%s", State.Build);
		}
		Add("lines", 0, "Log lines buffered across every view", "%lld lines", State.LogLines);
		Add("fps", 0, "UI frame rate", "%.0f FPS", ImGui::GetIO().Framerate);

		float RightWidth = 0.0f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			RightWidth += StatusCellWidth(Cells[Index].Text);
		}
		ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - RightWidth));

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const ImU32 Chip =
				Cells[Index].Tone == 1 ? RGBA(Palette::ChipOk)
				: Cells[Index].Tone == 2 ? RGBA(Palette::ChipWarn)
				: Cells[Index].Tone == 3 ? RGBA(Palette::ChipBad)
				: 0;
			StatusCell(Cells[Index].Id, Cells[Index].Text, Weak, true, Cells[Index].Tip, Chip);
		}

		// viewport already supplies the outside bottom edge
		DrawBarEdges(EBarEdge::Top);
	}
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

void EndFrameChrome()
{
	// last so the tab rects are this frame's
	DrawInactiveTabOutlines();
}

void ResizeBlur(int32 Width, int32 Height)
{
	GBlur.Resize(Width, Height);
}

void CaptureBlurIfIdle(IDXGISwapChain* Swap)
{
	// only when nothing frosted is on screen, or a popover blurs its own reflection
	if (!GGlassVisible)
	{
		GBlur.CaptureAndBlur(Swap, Tune().BlurStrength);
	}
}
}
