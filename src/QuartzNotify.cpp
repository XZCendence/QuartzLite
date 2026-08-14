#include "QuartzNotify.h"
#include "QuartzTypes.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "QuartzStyle.h"

#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

namespace QuartzNotify
{
namespace
{
	constexpr float RiseTime = 0.28f;
	constexpr float HoldTime = 5.4f;
	constexpr float FallTime = 0.45f;
	constexpr int32 MaxVisible = 4;

	struct FToast
	{
		std::string Title;
		std::string Body;
		ESeverity   Severity = ESeverity::Info;
		double      CreatedAt = 0.0;
		double      PausedFor = 0.0;   // hover time, holds the toast open
		int32     Id = 0;
		bool        bDismissed = false;
	};

	std::vector<FToast> GToasts;
	int32 GNextId = 1;
	bool GEnabled = true;

	ImVec4 SeverityAccent(ESeverity Severity)
	{
		switch (Severity)
		{
		case ESeverity::Success: return ImVec4(0.42f, 0.82f, 0.52f, 1.0f);
		case ESeverity::Failure: return ImVec4(0.92f, 0.44f, 0.46f, 1.0f);
		default:
			return ImGui::GetStyleColorVec4(ImGuiCol_TabSelectedOverline);
		}
	}

	float EaseOut(float T)
	{
		const float U = 1.0f - ImClamp(T, 0.0f, 1.0f);
		return 1.0f - U * U * U;
	}
}

bool& Enabled()
{
	return GEnabled;
}

void Push(const char* Title, const char* Body, ESeverity Severity)
{
	if (!GEnabled)
	{
		return;
	}

	FToast Toast;
	Toast.Title = Title;
	Toast.Body = Body;
	Toast.Severity = Severity;
	Toast.CreatedAt = ImGui::GetTime();
	Toast.Id = GNextId++;
	GToasts.push_back(std::move(Toast));

	while ((int32)GToasts.size() > MaxVisible)
	{
		GToasts.erase(GToasts.begin());
	}
}

void Draw()
{
	if (GToasts.empty())
	{
		return;
	}

	const double Now = ImGui::GetTime();
	const float Scale = UiDpi();
	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	const ImGuiIO& IO = ImGui::GetIO();
	const bool bOwnWindows = (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;

	// with viewports, sit against the work area of the monitor the app is on
	ImVec2 Area = Viewport->WorkPos;
	ImVec2 AreaSize = Viewport->WorkSize;
	if (bOwnWindows)
	{
		const ImGuiPlatformIO& PlatformIO = ImGui::GetPlatformIO();
		const ImVec2 AppCenter(Viewport->Pos.x + Viewport->Size.x * 0.5f,
			Viewport->Pos.y + Viewport->Size.y * 0.5f);
		for (const ImGuiPlatformMonitor& Monitor : PlatformIO.Monitors)
		{
			if (AppCenter.x >= Monitor.MainPos.x && AppCenter.x < Monitor.MainPos.x + Monitor.MainSize.x
				&& AppCenter.y >= Monitor.MainPos.y && AppCenter.y < Monitor.MainPos.y + Monitor.MainSize.y)
			{
				Area = Monitor.WorkPos;
				AreaSize = Monitor.WorkSize;
				break;
			}
		}
	}

	const float Margin = 12.0f * Scale;
	float StackY = Area.y + AreaSize.y - Margin;

	for (int32 Index = (int32)GToasts.size() - 1; Index >= 0; --Index)
	{
		FToast& Toast = GToasts[Index];
		const float Age = (float)(Now - Toast.CreatedAt - Toast.PausedFor);
		const float FadeStart = RiseTime + HoldTime;

		if (Toast.bDismissed || Age > FadeStart + FallTime)
		{
			GToasts.erase(GToasts.begin() + Index);
			continue;
		}

		float Progress = 1.0f;   // 0 fully out, 1 fully in
		if (Age < RiseTime)
		{
			Progress = EaseOut(Age / RiseTime);
		}
		else if (Age > FadeStart)
		{
			Progress = 1.0f - EaseOut((Age - FadeStart) / FallTime);
		}

		const float Alpha = ImClamp(Progress, 0.0f, 1.0f);
		if (Alpha <= 0.001f)
		{
			continue;
		}

		char WindowName[32];
		snprintf(WindowName, sizeof(WindowName), "##toast-%d", Toast.Id);

		// slide in from the right
		const float SlideIn = (1.0f - Progress) * 40.0f * Scale;
		const ImVec2 Anchor(Area.x + AreaSize.x - Margin + SlideIn, StackY);

		if (bOwnWindows)
		{
			// own platform window, always on top, never steals focus
			ImGuiWindowClass ToastClass;
			ToastClass.ViewportFlagsOverrideSet =
				ImGuiViewportFlags_NoAutoMerge | ImGuiViewportFlags_TopMost |
				ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_NoFocusOnAppearing;
			ImGui::SetNextWindowClass(&ToastClass);
		}
		// severity lives in the accent bar and title only. bg follows the theme
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
		ImGui::SetNextWindowPos(Anchor, ImGuiCond_Always, ImVec2(1.0f, 1.0f)); // bottom-right pivot
		ImGui::SetNextWindowBgAlpha(0.97f * Alpha);
		ImGui::SetNextWindowSizeConstraints(ImVec2(240.0f * Scale, 0.0f), ImVec2(420.0f * Scale, FLT_MAX));
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alpha);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13.0f * Scale, 11.0f * Scale));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f * Scale);

		const ImGuiWindowFlags Flags =
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

		if (ImGui::Begin(WindowName, nullptr, Flags))
		{
			const ImVec4 Accent = SeverityAccent(Toast.Severity);
			const ImVec2 Min = ImGui::GetWindowPos();
			const ImVec2 Size = ImGui::GetWindowSize();
			ImDrawList* DrawList = ImGui::GetWindowDrawList();

			DrawList->AddRectFilled(Min, ImVec2(Min.x + Size.x, Min.y + 3.0f * Scale),
				ImGui::GetColorU32(Accent), 6.0f * Scale, ImDrawFlags_RoundCornersTop);

			ImGui::TextColored(Accent, "%s", Toast.Title.c_str());
			if (!Toast.Body.empty())
			{
				ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
				ImGui::TextUnformatted(Toast.Body.c_str());
				ImGui::PopTextWrapPos();
			}

			// hover holds it open. click dismisses
			if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
			{
				Toast.PausedFor += ImGui::GetIO().DeltaTime;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					Toast.bDismissed = true;
				}
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			}

			StackY -= Size.y + 8.0f * Scale;
		}
		ImGui::End();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor();
	}
}
}
