#include "QuartzLogView.h"
#include "QuartzTypes.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "QuartzStyle.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	ImFont* GMonoFont = nullptr;

	ImVec4 LevelColour(ELogLevel Level)
	{
		switch (Level)
		{
		case ELogLevel::Fatal:
		case ELogLevel::Error:       return ImVec4(0.88f, 0.42f, 0.46f, 1.0f);
		case ELogLevel::Warning:     return ImVec4(0.90f, 0.74f, 0.35f, 1.0f);
		case ELogLevel::Verbose:
		case ELogLevel::VeryVerbose: return ImVec4(0.56f, 0.60f, 0.63f, 1.0f);
		case ELogLevel::Display:     return ImVec4(0.38f, 0.68f, 0.96f, 1.0f);
		default:                     return ImVec4(0.72f, 0.77f, 0.79f, 1.0f);
		}
	}

	const char* LevelName(ELogLevel Level)
	{
		switch (Level)
		{
		case ELogLevel::Fatal:       return "FATAL";
		case ELogLevel::Error:       return "ERROR";
		case ELogLevel::Warning:     return "WARN";
		case ELogLevel::Display:     return "DISPLAY";
		case ELogLevel::Verbose:     return "VERBOSE";
		case ELogLevel::VeryVerbose: return "VVERBOSE";
		default:                     return "LOG";
		}
	}

	enum class ETimeFormat : int32 { H24, H12, Relative, Count };
	constexpr const char* GTimeFormatLabel[3] = { "TIME", "TIME 12H", "AGE" };
	constexpr const char* GTimeFormatDesc[3] = { "24-hour", "12-hour", "relative age" };

	void FormatTimeCell(char* Out, int32 OutSize, const FLogLine& Line, int32 Format, double Now)
	{
		if (Format == (int32)ETimeFormat::H12 && Line.Hour >= 0)
		{
			const int32 Hour12 = Line.Hour % 12 == 0 ? 12 : Line.Hour % 12;
			snprintf(Out, OutSize, "%d:%02d:%02d.%03d %s", Hour12, Line.Minute, Line.Second,
				Line.Millisecond, Line.Hour < 12 ? "AM" : "PM");
		}
		else if (Format == (int32)ETimeFormat::Relative && Line.AddedAt > 0.0)
		{
			const int64 Sec = (int64)ImMax(0.0, Now - Line.AddedAt);
			if (Sec < 60)
			{
				snprintf(Out, OutSize, "%llds ago", Sec);
			}
			else if (Sec < 3600)
			{
				snprintf(Out, OutSize, "%lldm%02llds ago", Sec / 60, Sec % 60);
			}
			else
			{
				snprintf(Out, OutSize, "%lldh%02lldm ago", Sec / 3600, (Sec % 3600) / 60);
			}
		}
		else
		{
			snprintf(Out, OutSize, "%s", Line.Timestamp.empty() ? "--" : Line.Timestamp.c_str());
		}
	}

	// widths are unscaled units, dpi-scaled at draw time. MESSAGE takes whatever is left
	constexpr const char* GColNames[5] = { "TIME", "FRAME", "CATEGORY", "LEVEL", "MESSAGE" };
	constexpr float GColDefault[4] = { 104.0f, 60.0f, 218.0f, 90.0f };
	constexpr float GColMin[4] = { 60.0f, 44.0f, 70.0f, 58.0f };
	float GColWidth[4] = { GColDefault[0], GColDefault[1], GColDefault[2], GColDefault[3] };
	int32 GTimeFormat = (int32)ETimeFormat::H24;   // clicking the TIME header cycles this
	bool GFreshFlash = true;                       // amber flash on newly arrived lines

	struct FViewState
	{
		std::unordered_set<int64> Selected;   // stable line ids
		int64 SelectionAnchor = -1;   // pivot for shift-click ranges
		int64 PruneBase = 0;          // last eviction count the selection was pruned against
		int32 CopiedCount = 0;        // "copied N lines" header flash
		double CopiedFlashUntil = 0.0;
		bool bFollowTail = true;
		bool bScrollInitialized = false;
		float SmoothY = 0.0f;
		float TargetY = 0.0f;
		// header lives in the parent, so it follows the child's last scrollx by hand
		float ScrollX = 0.0f;
		int64 ScrollDropBase = 0;     // eviction count the scroll was last rebased against
		// widest MESSAGE seen while on screen. only ever grown, so the scrollbar does not twitch
		float MaxMsgWidth = 0.0f;
		float MaxMsgWidthScale = 0.0f;

		bool bSearchOpen = false;
		bool bSearchFocusPending = false;
		char SearchQuery[128] = {};
		bool bSearchCase = false;
		float SearchAnim = 0.0f;        // 0 closed, 1 open
		std::vector<int64> Matches;
		int32 CurrentMatch = -1;
		std::string IndexedQuery;       // query the index was built for
		bool bIndexedCase = false;
		int64 IndexedThrough = 0;     // absolute id the index has consumed up to
		bool bScrollToMatch = false;
	};
	std::unordered_map<const FLogSource*, FViewState> GViewStates;

	const char* FindMatch(const char* Haystack, const char* Needle, bool bCaseSensitive)
	{
		if (!Needle || !*Needle)
		{
			return nullptr;
		}
		if (bCaseSensitive)
		{
			return strstr(Haystack, Needle);
		}
		for (const char* H = Haystack; *H; ++H)
		{
			const char* A = H;
			const char* B = Needle;
			while (*A && *B && tolower((unsigned char)*A) == tolower((unsigned char)*B))
			{
				++A;
				++B;
			}
			if (!*B)
			{
				return H;
			}
		}
		return nullptr;
	}

	bool LineMatchesQuery(const FLogLine& Line, const char* Query, bool bCaseSensitive)
	{
		return FindMatch(Line.Text.c_str(), Query, bCaseSensitive) != nullptr
			|| FindMatch(Line.Category.c_str(), Query, bCaseSensitive) != nullptr;
	}
}

void SetLogViewMonoFont(ImFont* Font)
{
	GMonoFont = Font;
}

bool& LogViewFreshFlashEnabled()
{
	return GFreshFlash;
}

void RegisterLogViewSettings()
{
	ImGuiSettingsHandler Handler;
	Handler.TypeName = "QuartzLogView";
	Handler.TypeHash = ImHashStr("QuartzLogView");
	Handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char*) -> void*
	{
		return (void*)1;
	};
	Handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* Line)
	{
		float Widths[4];
		int32 Format;
		if (sscanf(Line, "Cols=%f,%f,%f,%f", &Widths[0], &Widths[1], &Widths[2], &Widths[3]) == 4)
		{
			// clamp so a hand-edited ini cannot make a column too narrow to grab
			for (int32 Col = 0; Col < 4; ++Col)
			{
				GColWidth[Col] = ImClamp(Widths[Col], GColMin[Col], 900.0f);
			}
		}
		else if (sscanf(Line, "TimeFormat=%d", &Format) == 1)
		{
			GTimeFormat = ImClamp(Format, 0, (int32)ETimeFormat::Count - 1);
		}
		else if (sscanf(Line, "FreshFlash=%d", &Format) == 1)
		{
			GFreshFlash = Format != 0;
		}
	};
	Handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* H, ImGuiTextBuffer* Buf)
	{
		Buf->appendf("[%s][View]\n", H->TypeName);
		Buf->appendf("Cols=%.0f,%.0f,%.0f,%.0f\n", GColWidth[0], GColWidth[1], GColWidth[2], GColWidth[3]);
		Buf->appendf("TimeFormat=%d\n", GTimeFormat);
		Buf->appendf("FreshFlash=%d\n\n", GFreshFlash ? 1 : 0);
	};
	ImGui::AddSettingsHandler(&Handler);
}

void DrawLogWindow(FLogSource& Source)
{
	if (!ImGui::Begin(Source.Name.c_str(), &Source.bOpen))
	{
		ImGui::End();
		return;
	}

	const std::vector<FLogLine>& Items = Source.GetItems();
	FViewState& State = GViewStates[&Source];

	const int64 IdBase = Source.GetDroppedCount();
	if (State.PruneBase != IdBase)
	{
		for (auto It = State.Selected.begin(); It != State.Selected.end();)
		{
			It = (*It < IdBase) ? State.Selected.erase(It) : ++It;
		}
		State.PruneBase = IdBase;
	}

	// search index. query change rescans once, then only new lines
	{
		const std::string Query(State.SearchQuery);
		const int64 End = IdBase + (int64)Items.size();

		if (Query != State.IndexedQuery || State.bSearchCase != State.bIndexedCase)
		{
			State.Matches.clear();
			State.IndexedQuery = Query;
			State.bIndexedCase = State.bSearchCase;
			State.IndexedThrough = IdBase;
			State.CurrentMatch = -1;
		}
		else if (State.IndexedThrough < IdBase)
		{
			State.IndexedThrough = IdBase;   // buffer wrapped past where we had scanned
		}

		if (!State.Matches.empty() && State.Matches[0] < IdBase)
		{
			int32 Drop = 0;
			while (Drop < (int32)State.Matches.size() && State.Matches[Drop] < IdBase)
			{
				++Drop;
			}
			State.Matches.erase(State.Matches.begin(), State.Matches.begin() + Drop);
			State.CurrentMatch = State.Matches.empty() ? -1
				: ImClamp(State.CurrentMatch - Drop, 0, (int32)State.Matches.size() - 1);
		}

		if (!Query.empty())
		{
			for (int64 Id = ImMax(State.IndexedThrough, IdBase); Id < End; ++Id)
			{
				if (LineMatchesQuery(Items[(size_t)(Id - IdBase)], Query.c_str(), State.bSearchCase))
				{
					State.Matches.push_back(Id);
				}
			}
			if (State.CurrentMatch == -1 && !State.Matches.empty())
			{
				State.CurrentMatch = (int32)State.Matches.size() - 1;
				State.bScrollToMatch = true;
			}
		}
		State.IndexedThrough = End;
	}

	if (ImGui::GetTime() < State.CopiedFlashUntil)
	{
		ImGui::TextDisabled("%d lines", (int32)Items.size());
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.55f, 0.80f, 0.55f, 1.0f), "copied %d line%s",
			State.CopiedCount, State.CopiedCount == 1 ? "" : "s");
	}
	else if (!State.Selected.empty())
	{
		ImGui::TextDisabled("%d lines  --  %d selected  (Ctrl+C copies, Esc clears)",
			(int32)Items.size(), (int32)State.Selected.size());
	}
	else
	{
		ImGui::TextDisabled("%d lines", (int32)Items.size());
	}

	bool bWantsClear = false;   // deferred. clearing mid-draw pulls the row array out
	{
		const float BtnW = 46.0f * UiDpi();
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - BtnW - ImGui::GetStyle().WindowPadding.x);
		ImGui::BeginDisabled(Items.empty());
		if (ImGui::SmallButton("Clear"))
		{
			bWantsClear = true;
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("Discard every buffered line in this view (Ctrl+L)");
		}
	}
	ImGui::Separator();

	auto CopySelection = [&Items, &State, IdBase](bool bMessageOnly)
	{
		std::string Out;
		int32 Count = 0;
		for (int32 Row = 0; Row < (int32)Items.size(); ++Row)
		{
			if (State.Selected.count(IdBase + Row) == 0)
			{
				continue;
			}
			const FLogLine& Line = Items[Row];
			if (!bMessageOnly)
			{
				char Prefix[192];
				snprintf(Prefix, sizeof(Prefix), "[%s] %s: %s: ",
					Line.Timestamp.empty() ? "--" : Line.Timestamp.c_str(),
					Line.Category.empty() ? "-" : Line.Category.c_str(), LevelName(Line.Level));
				Out += Prefix;
			}
			Out += Line.Text;
			Out += "\r\n";
			++Count;
		}
		if (Count > 0)
		{
			ImGui::SetClipboardText(Out.c_str());
			State.CopiedCount = Count;
			State.CopiedFlashUntil = ImGui::GetTime() + 1.4;
		}
	};
	auto SelectAll = [&Items, &State, IdBase]()
	{
		State.Selected.clear();
		State.Selected.reserve(Items.size());
		for (int32 Row = 0; Row < (int32)Items.size(); ++Row)
		{
			State.Selected.insert(IdBase + Row);
		}
	};
	bool bOpenContextMenu = false;

	const float Scale = UiDpi();
	float ColX[5];
	{
		float X = 8.0f * Scale;
		for (int32 Col = 0; Col < 5; ++Col)
		{
			ColX[Col] = X;
			if (Col < 4)
			{
				X += GColWidth[Col] * Scale;
			}
		}
	}

	{
		const float HeaderH = ImGui::GetTextLineHeight() + 7.0f * Scale;
		const ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
		const float HeaderW = ImGui::GetContentRegionAvail().x;
		ImGui::Dummy(ImVec2(HeaderW, HeaderH));
		const ImVec2 AfterHeader = ImGui::GetCursorScreenPos();
		ImDrawList* HeaderDraw = ImGui::GetWindowDrawList();
		HeaderDraw->AddRectFilled(HeaderMin, ImVec2(HeaderMin.x + HeaderW, HeaderMin.y + HeaderH), IM_COL32(0, 0, 0, 28));
		const float TextY = HeaderMin.y + (HeaderH - ImGui::GetTextLineHeight()) * 0.5f;
		const ImU32 HeaderCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		const ImVec4 HeaderClip(HeaderMin.x, HeaderMin.y, HeaderMin.x + HeaderW, HeaderMin.y + HeaderH);
		for (int32 Col = 0; Col < 5; ++Col)
		{
			const ImVec2 TextPos(HeaderMin.x + ColX[Col] - State.ScrollX, TextY);
			const char* Name = (Col == 0) ? GTimeFormatLabel[GTimeFormat] : GColNames[Col];
			HeaderDraw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), TextPos, HeaderCol,
				Name, nullptr, 0.0f, &HeaderClip);
		}
		// before the grips so the boundary grip wins the overlap
		{
			ImGui::SetCursorScreenPos(ImVec2(HeaderMin.x + ColX[0] - State.ScrollX, HeaderMin.y));
			const float ClickW = ImMax((GColWidth[0] - 14.0f) * Scale, 24.0f * Scale);
			ImGui::InvisibleButton("##time-format", ImVec2(ClickW, HeaderH));
			if (ImGui::IsItemHovered())
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				ImGui::SetTooltip("Time format: %s (click to cycle)", GTimeFormatDesc[GTimeFormat]);
			}
			if (ImGui::IsItemClicked())
			{
				GTimeFormat = (GTimeFormat + 1) % (int32)ETimeFormat::Count;
				ImGui::MarkIniSettingsDirty();
			}
		}
		for (int32 Col = 0; Col < 4; ++Col)
		{
			const float BoundaryX = HeaderMin.x + ColX[Col + 1] - 6.0f * Scale - State.ScrollX;
			if (BoundaryX < HeaderMin.x || BoundaryX > HeaderMin.x + HeaderW)
			{
				continue;
			}
			ImGui::PushID(Col);
			ImGui::SetCursorScreenPos(ImVec2(BoundaryX - 4.0f * Scale, HeaderMin.y));
			ImGui::InvisibleButton("##col-grip", ImVec2(8.0f * Scale, HeaderH));
			const bool bGripHovered = ImGui::IsItemHovered();
			const bool bGripActive = ImGui::IsItemActive();
			if (bGripHovered || bGripActive)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
			}
			if (bGripActive && ImGui::GetIO().MouseDelta.x != 0.0f)
			{
				GColWidth[Col] = ImClamp(GColWidth[Col] + ImGui::GetIO().MouseDelta.x / Scale,
					GColMin[Col], 900.0f);
				ImGui::MarkIniSettingsDirty();
			}
			if (bGripHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				GColWidth[Col] = GColDefault[Col];
				ImGui::MarkIniSettingsDirty();
			}
			const ImU32 GripCol = (bGripHovered || bGripActive)
				? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
				: IM_COL32(255, 255, 255, 26);
			HeaderDraw->AddLine(ImVec2(BoundaryX, HeaderMin.y + 3.0f * Scale),
				ImVec2(BoundaryX, HeaderMin.y + HeaderH - 3.0f * Scale), GripCol, bGripActive ? 2.0f : 1.0f);
			ImGui::PopID();
		}
		ImGui::SetCursorScreenPos(AfterHeader);
	}

	if (GMonoFont)
	{
		ImGui::PushFont(GMonoFont);
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
	if (ImGui::BeginChild("##log-scroll", ImVec2(0, 0), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar))
	{
		const float RowPitch = ImGui::GetTextLineHeight() + 7.0f * Scale;
		const ImVec2 Avail = ImGui::GetContentRegionAvail();
		if (State.MaxMsgWidthScale != Scale)
		{
			State.MaxMsgWidth = 0.0f;
			State.MaxMsgWidthScale = Scale;
		}
		const float RowWidth = ImMax(Avail.x,
			ColX[4] + ImMax(State.MaxMsgWidth, 400.0f * Scale) + 24.0f * Scale);
		const float ContentH = (float)Items.size() * RowPitch;
		State.ScrollX = ImGui::GetScrollX();
		const float MaxScrollY = ImMax(0.0f, ContentH - Avail.y);   // GetScrollMaxY trails by one frame
		const float RawScrollY = ImGui::GetScrollY();
		if (!State.bScrollInitialized)
		{
			State.SmoothY = State.TargetY = RawScrollY;
			State.bScrollInitialized = true;
		}

		if (State.bScrollToMatch && State.CurrentMatch >= 0
			&& State.CurrentMatch < (int32)State.Matches.size())
		{
			const int64 Row = State.Matches[State.CurrentMatch] - IdBase;
			if (Row >= 0 && Row < (int64)Items.size())
			{
				State.bFollowTail = false;
				State.TargetY = ImClamp((float)Row * RowPitch - Avail.y * 0.5f + RowPitch * 0.5f,
					0.0f, MaxScrollY);
			}
			State.bScrollToMatch = false;
		}

		// once the ring is full, arriving lines evict from the front instead of growing
		// the content. rebase the scroll by the eviction count so the view stays put
		const int64 NewlyDropped = IdBase - State.ScrollDropBase;
		const bool bEvicted = NewlyDropped > 0;
		if (bEvicted)
		{
			const float Shift = (float)NewlyDropped * RowPitch;
			State.SmoothY = ImMax(0.0f, State.SmoothY - Shift);
			State.TargetY = ImMax(0.0f, State.TargetY - Shift);
		}
		State.ScrollDropBase = IdBase;

		const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
		const float Wheel = bHovered ? ImGui::GetIO().MouseWheel : 0.0f;
		if (Wheel != 0.0f)
		{
			if (Wheel > 0.0f)
			{
				State.bFollowTail = false;
			}
			State.TargetY = ImClamp(State.TargetY - Wheel * RowPitch * 3.0f, 0.0f, MaxScrollY);
		}
		else if (!State.bFollowTail && !bEvicted && fabsf(RawScrollY - State.SmoothY) > 2.0f)
		{
			// skip on an eviction frame. the rebase moves SmoothY away from the applied
			// scroll, which this would otherwise read as a drag and undo
			State.TargetY = ImClamp(RawScrollY, 0.0f, MaxScrollY);
		}
		if (MaxScrollY <= 0.0f || (!State.bFollowTail && MaxScrollY - State.TargetY <= 2.0f))
		{
			State.bFollowTail = true;
		}
		if (State.bFollowTail)
		{
			State.TargetY = MaxScrollY;
		}
		State.TargetY = ImClamp(State.TargetY, 0.0f, MaxScrollY);
		State.SmoothY = ImClamp(State.SmoothY, 0.0f, MaxScrollY);
		const float Alpha = 1.0f - expf(-22.0f * ImGui::GetIO().DeltaTime);
		State.SmoothY = ImLerp(State.SmoothY, State.TargetY, Alpha);
		if (fabsf(State.TargetY - State.SmoothY) < 0.25f)
		{
			State.SmoothY = State.TargetY;
		}
		// SetScrollY takes effect on the next begin. virtualize from this frame's raw
		// position or fast motion can request rows outside the clip rect
		ImGui::SetScrollY(State.SmoothY);
		const float ScrollY = RawScrollY;

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		ImGui::Dummy(ImVec2(RowWidth, ContentH));
		const int32 First = ImClamp((int32)(ScrollY / RowPitch) - 2, 0, (int32)Items.size());
		const int32 Last = ImClamp((int32)((ScrollY + Avail.y) / RowPitch) + 3, 0, (int32)Items.size());
		ImDrawList* Draw = ImGui::GetWindowDrawList();
		float FrameMaxMsgWidth = 0.0f;
		ImFont* RowFont = ImGui::GetFont();
		const float RowFontSize = ImGui::GetFontSize();
		const double Now = ImGui::GetTime();
		for (int32 Row = First; Row < Last; ++Row)
		{
			const FLogLine& Line = Items[Row];
			const ImVec2 RowMin(Origin.x, Origin.y + Row * RowPitch);
			const ImVec2 RowMax(RowMin.x + RowWidth, RowMin.y + RowPitch);
			ImGui::PushID(Row);
			ImGui::SetCursorScreenPos(RowMin);
			ImGui::InvisibleButton("##row", ImVec2(RowWidth, RowPitch));
			const bool bRowHovered = ImGui::IsItemHovered();
			const int64 LineId = IdBase + Row;
			const bool bSelected = State.Selected.count(LineId) != 0;
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
			{
				const ImGuiIO& ClickIO = ImGui::GetIO();
				if (ClickIO.KeyShift && State.SelectionAnchor != -1)
				{
					if (!ClickIO.KeyCtrl)
					{
						State.Selected.clear();
					}
					const int64 Lo = ImMax(ImMin(State.SelectionAnchor, LineId), IdBase);
					const int64 Hi = ImMin(ImMax(State.SelectionAnchor, LineId),
						IdBase + (int64)Items.size() - 1);
					for (int64 Id = Lo; Id <= Hi; ++Id)
					{
						State.Selected.insert(Id);
					}
				}
				else if (ClickIO.KeyCtrl)
				{
					if (bSelected)
					{
						State.Selected.erase(LineId);
					}
					else
					{
						State.Selected.insert(LineId);
					}
					State.SelectionAnchor = LineId;
				}
				else
				{
					State.Selected.clear();
					State.Selected.insert(LineId);
					State.SelectionAnchor = LineId;
				}
				State.bFollowTail = false;
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
			{
				if (!bSelected)
				{
					State.Selected.clear();
					State.Selected.insert(LineId);
					State.SelectionAnchor = LineId;
				}
				bOpenContextMenu = true;
			}
			if (Row & 1)
			{
				Draw->AddRectFilled(RowMin, RowMax, ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
			}
			if (bSelected)
			{
				Draw->AddRectFilled(RowMin, RowMax, ImGui::GetColorU32(ImGuiCol_Header, 0.75f));
			}
			const float Freshness = (!GFreshFlash || Line.AddedAt <= 0.0) ? 0.0f
				: ImClamp(1.0f - (float)((Now - Line.AddedAt) / 2.6), 0.0f, 1.0f);
			if (Freshness > 0.0f)
			{
				Draw->AddRectFilled(RowMin, RowMax, IM_COL32(231, 174, 66, (int32)(112.0f * Freshness * Freshness)));
				Draw->AddRectFilled(RowMin, ImVec2(RowMin.x + 2.0f * Scale, RowMax.y),
					IM_COL32(255, 202, 84, (int32)(235.0f * Freshness)));
			}
			if (bRowHovered)
			{
				Draw->AddRectFilled(RowMin, RowMax, IM_COL32(255, 255, 255, 16));
			}
			const float Baseline = RowMin.y + (RowPitch - ImGui::GetTextLineHeight()) * 0.5f;
			char FrameText[16] = "--";
			if (Line.Frame != -1)
			{
				snprintf(FrameText, sizeof(FrameText), "%d", Line.Frame);
			}
			const ImU32 Muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			const ImVec4 TimeClip(RowMin.x + ColX[0], RowMin.y, RowMin.x + ColX[1] - 8.0f * Scale, RowMax.y);
			const ImVec4 FrameClip(RowMin.x + ColX[1], RowMin.y, RowMin.x + ColX[2] - 8.0f * Scale, RowMax.y);
			const ImVec4 CatClip(RowMin.x + ColX[2], RowMin.y, RowMin.x + ColX[3] - 8.0f * Scale, RowMax.y);
			const ImVec4 LevelClip(RowMin.x + ColX[3], RowMin.y, RowMin.x + ColX[4] - 8.0f * Scale, RowMax.y);
			char TimeText[48];
			FormatTimeCell(TimeText, sizeof(TimeText), Line, GTimeFormat, Now);
			Draw->AddText(RowFont, RowFontSize, ImVec2(RowMin.x + ColX[0], Baseline), Muted, TimeText, nullptr, 0.0f, &TimeClip);
			Draw->AddText(RowFont, RowFontSize, ImVec2(RowMin.x + ColX[1], Baseline), Muted, FrameText, nullptr, 0.0f, &FrameClip);
			if (!Line.Category.empty())
			{
				Draw->AddText(RowFont, RowFontSize, ImVec2(RowMin.x + ColX[2], Baseline), Muted, Line.Category.c_str(), nullptr, 0.0f, &CatClip);
			}
			const char* Level = LevelName(Line.Level);
			const ImVec4 LevelColor = LevelColour(Line.Level);
			const ImVec2 LevelPos(RowMin.x + ColX[3], Baseline);
			const ImVec2 LevelSize = ImGui::CalcTextSize(Level);
			Draw->AddRectFilled(ImVec2(LevelPos.x - 5.0f * Scale, RowMin.y + 3.0f * Scale),
				ImVec2(ImMin(LevelPos.x + LevelSize.x + 5.0f * Scale, LevelClip.z), RowMax.y - 3.0f * Scale),
				ImGui::GetColorU32(ImVec4(LevelColor.x, LevelColor.y, LevelColor.z, 0.12f)), 3.0f * Scale);
			Draw->AddText(RowFont, RowFontSize, LevelPos, ImGui::GetColorU32(LevelColor), Level, nullptr, 0.0f, &LevelClip);
			const bool bEmphasis = Line.Level == ELogLevel::Fatal || Line.Level == ELogLevel::Error
				|| Line.Level == ELogLevel::Warning;
			const ImVec2 MsgPos(RowMin.x + ColX[4], Baseline);
			const ImU32 MsgCol = bEmphasis ? ImGui::GetColorU32(LevelColor) : ImGui::GetColorU32(ImGuiCol_Text);
			const char* Msg = Line.Text.c_str();

			if (State.SearchQuery[0] != 0)
			{
				const bool bCurrentRow = State.CurrentMatch >= 0
					&& State.CurrentMatch < (int32)State.Matches.size()
					&& State.Matches[State.CurrentMatch] == LineId;
				const int32 QueryLen = (int32)strlen(State.SearchQuery);
				const char* Cursor = Msg;
				while (const char* Hit = FindMatch(Cursor, State.SearchQuery, State.bSearchCase))
				{
					const float X0 = ImGui::CalcTextSize(Msg, Hit).x;
					const float X1 = X0 + ImGui::CalcTextSize(Hit, Hit + QueryLen).x;
					Draw->AddRectFilled(
						ImVec2(MsgPos.x + X0 - 1.0f * Scale, RowMin.y + 2.0f * Scale),
						ImVec2(MsgPos.x + X1 + 1.0f * Scale, RowMax.y - 2.0f * Scale),
						bCurrentRow ? IM_COL32(255, 165, 0, 150) : IM_COL32(255, 235, 60, 70),
						2.0f * Scale);
					Cursor = Hit + QueryLen;
				}
			}
			Draw->AddText(MsgPos, MsgCol, Msg);
			FrameMaxMsgWidth = ImMax(FrameMaxMsgWidth, ImGui::CalcTextSize(Msg).x);
			ImGui::PopID();
		}
		State.MaxMsgWidth = ImMax(State.MaxMsgWidth, FrameMaxMsgWidth);

		const ImVec2 ViewMin = ImGui::GetWindowPos();
		const ImVec2 ViewSize = ImGui::GetWindowSize();
		const ImVec2 ViewMax(ViewMin.x + ViewSize.x, ViewMin.y + ViewSize.y);
		const float ShadowH = 14.0f * Scale;
		Draw->AddRectFilledMultiColor(
			ViewMin, ImVec2(ViewMax.x, ViewMin.y + ShadowH),
			IM_COL32(0, 0, 0, 92), IM_COL32(0, 0, 0, 92),
			IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	if (GMonoFont)
	{
		ImGui::PopFont();
	}

	const bool bViewActive = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		|| ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

	auto StepMatch = [&State](int32 Delta)
	{
		if (State.Matches.empty())
		{
			return;
		}
		const int32 Count = (int32)State.Matches.size();
		State.CurrentMatch = State.CurrentMatch == -1
			? (Delta > 0 ? 0 : Count - 1)
			: ((State.CurrentMatch + Delta) % Count + Count) % Count;   // wraps both ways
		State.bScrollToMatch = true;
	};

	if (bViewActive && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F))
	{
		State.bSearchOpen = !State.bSearchOpen;
		State.bSearchFocusPending = State.bSearchOpen;
	}

	const float AnimTarget = State.bSearchOpen ? 1.0f : 0.0f;
	State.SearchAnim += (AnimTarget - State.SearchAnim)
		* (1.0f - expf(-18.0f * ImGui::GetIO().DeltaTime));
	if (fabsf(AnimTarget - State.SearchAnim) < 0.002f)
	{
		State.SearchAnim = AnimTarget;
	}

	// closing the bar ends the search. imgui caches InputText that stops being drawn
	// while active, so clear the cache or it re-applies over the buffer on reopen
	if (!State.bSearchOpen && State.SearchQuery[0] != 0)
	{
		State.SearchQuery[0] = 0;
		State.Matches.clear();
		State.CurrentMatch = -1;
		if (ImGuiContext* Ctx = ImGui::GetCurrentContext())
		{
			Ctx->InputTextDeactivatedState.ID = 0;
		}
	}

	bool bSearchFieldActive = false;   // stand down the view's ctrl+a / ctrl+c while the field has focus

	if (State.SearchAnim > 0.004f)
	{
		const float BarW = 428.0f * Scale;
		const float BarH = 34.0f * Scale;
		const ImVec2 WinMin = ImGui::GetWindowPos();
		const float Slide = (1.0f - State.SearchAnim) * BarH;
		const ImVec2 BarPos(WinMin.x + ImGui::GetWindowWidth() - BarW - 16.0f * Scale,
			WinMin.y + ImGui::GetCurrentWindow()->TitleBarHeight + 4.0f * Scale - Slide);

		ImGui::SetCursorScreenPos(BarPos);
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * State.SearchAnim);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f * Scale);
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
		if (ImGui::BeginChild("##search-bar", ImVec2(BarW, BarH), ImGuiChildFlags_Borders,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::SetCursorPos(ImVec2(7.0f * Scale, 5.0f * Scale));
			ImGui::SetNextItemWidth(196.0f * Scale);
			if (State.bSearchFocusPending)
			{
				ImGui::SetKeyboardFocusHere();
				State.bSearchFocusPending = false;
			}
			const bool bSubmitted = ImGui::InputTextWithHint("##query", "Find",
				State.SearchQuery, sizeof(State.SearchQuery),
				ImGuiInputTextFlags_EnterReturnsTrue);   // return advances instead of deactivating
			const bool bTyping = ImGui::IsItemActive();
			bSearchFieldActive = bTyping;
			if (bSubmitted)
			{
				StepMatch(ImGui::GetIO().KeyShift ? -1 : 1);
				ImGui::SetKeyboardFocusHere(-1);   // keep the caret for the next return
			}

			ImGui::SameLine(0.0f, 6.0f * Scale);
			if (ImGui::Selectable("Aa", State.bSearchCase, 0, ImVec2(22.0f * Scale, 0)))
			{
				State.bSearchCase = !State.bSearchCase;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Match case");
			}

			ImGui::SameLine(0.0f, 8.0f * Scale);
			char Counter[32];
			if (State.SearchQuery[0] == 0)
			{
				snprintf(Counter, sizeof(Counter), " ");
			}
			else if (State.Matches.empty())
			{
				snprintf(Counter, sizeof(Counter), "No results");
			}
			else
			{
				snprintf(Counter, sizeof(Counter), "%d/%d", State.CurrentMatch + 1, (int32)State.Matches.size());
			}
			ImGui::TextDisabled("%s", Counter);

			ImGui::SameLine(0.0f, 8.0f * Scale);
			ImGui::BeginDisabled(State.Matches.empty());
			if (ImGui::ArrowButton("##prev", ImGuiDir_Up))
			{
				StepMatch(-1);
			}
			ImGui::SameLine(0.0f, 3.0f * Scale);
			if (ImGui::ArrowButton("##next", ImGuiDir_Down))
			{
				StepMatch(1);
			}
			ImGui::EndDisabled();

			ImGui::SameLine(0.0f, 6.0f * Scale);
			if (ImGui::SmallButton("X"))
			{
				State.bSearchOpen = false;
			}

			if (bTyping || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				if (ImGui::IsKeyPressed(ImGuiKey_Escape))
				{
					State.bSearchOpen = false;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				{
					StepMatch(1);
				}
				if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				{
					StepMatch(-1);
				}
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
	}

	if (bViewActive && State.bSearchOpen && ImGui::IsKeyPressed(ImGuiKey_F3))
	{
		StepMatch(ImGui::GetIO().KeyShift ? -1 : 1);
	}

	if (bViewActive && !bSearchFieldActive)
	{
		if (!State.Selected.empty() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C))
		{
			CopySelection(/*bMessageOnly*/ false);
		}
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A))
		{
			SelectAll();
		}
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L))
		{
			bWantsClear = true;
		}
		if (!State.Selected.empty() && !ImGui::IsPopupOpen("log-context") && !State.bSearchOpen
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			State.Selected.clear();
			State.SelectionAnchor = -1;
		}
	}

	// window scope, not inside the row's PushID, so the menu keeps a stable id
	if (bOpenContextMenu)
	{
		ImGui::OpenPopup("log-context");
	}
	if (ImGui::BeginPopup("log-context"))
	{
		const int32 SelNum = (int32)State.Selected.size();
		char CopyLabel[48];
		snprintf(CopyLabel, sizeof(CopyLabel), "Copy %d line%s", SelNum, SelNum == 1 ? "" : "s");
		if (ImGui::MenuItem(CopyLabel, "Ctrl+C"))
		{
			CopySelection(/*bMessageOnly*/ false);
		}
		if (ImGui::MenuItem("Copy message text only"))
		{
			CopySelection(/*bMessageOnly*/ true);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Select all", "Ctrl+A"))
		{
			SelectAll();
		}
		if (ImGui::MenuItem("Clear selection", "Esc"))
		{
			State.Selected.clear();
			State.SelectionAnchor = -1;
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Clear log", "Ctrl+L"))
		{
			bWantsClear = true;
		}
		ImGui::EndPopup();
	}
	ImGui::End();

	if (bWantsClear)
	{
		Source.Clear();
		State.Selected.clear();
		State.SelectionAnchor = -1;
		State.bFollowTail = true;
		State.SmoothY = State.TargetY = 0.0f;
		State.bScrollInitialized = false;
		State.MaxMsgWidth = 0.0f;
	}
}
