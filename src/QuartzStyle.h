#pragma once

#include "QuartzTypes.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <math.h>
#include <unordered_map>

inline ImU32 RGBA(uint32 Value)
{
	return IM_COL32((Value >> 24) & 0xff, (Value >> 16) & 0xff, (Value >> 8) & 0xff, Value & 0xff);
}

inline ImVec4 RGBAv(uint32 Value)
{
	return ImVec4(((Value >> 24) & 0xff) / 255.f, ((Value >> 16) & 0xff) / 255.f,
		((Value >> 8) & 0xff) / 255.f, (Value & 0xff) / 255.f);
}

inline ImU32 WithAlpha(ImU32 Color, float Alpha)
{
	return (Color & 0x00FFFFFF) | ((ImU32)(((Color >> IM_COL32_A_SHIFT) & 0xff) * Alpha) << IM_COL32_A_SHIFT);
}

namespace Palette
{
	constexpr uint32 Hover = 0x85bff0ff;
	constexpr uint32 DropShadow = 0x0000008fu;

	// status bar surfaces
	constexpr uint32 StatusIdle = 0x22262bff;
	constexpr uint32 StatusWeakText = 0xb3c0c6ff;

	// chip fills for the state cells
	constexpr uint32 ChipOk = 0x2f9e63ff;
	constexpr uint32 ChipWarn = 0xd9a521ff;
	constexpr uint32 ChipBad = 0xc0483fff;
	constexpr uint32 ChipIdle = 0x343b42ff;
}

// tuned params we've settled on 
struct FUiTune
{
	float TabRadiusEm = 0.38f;
	float ShadowPad = 5.0f;
	float ShadowOffset = 2.0f;
	float BlurStrength = 12.0f;
	float AnimSpeed = 42.0f;
	float MenuSpeed = 52.0f;
	float StatusBarEm = 1.35f;
	float StatusPadEm = 0.42f;
	float TabBorderAlpha = 0.35f;   // idle-tab outline, over the dark bar
	float FrameEdgeAlpha = 0.50f;   // accent outline on buttons/headers/frames
	float GlassTint = 0.55f;        // popup fill opacity over the blurred backdrop
	bool  bAnimations = true;
};

inline FUiTune& Tune()
{
	static FUiTune Tuning;
	return Tuning;
}

inline float& UiDpi()
{
	static float Scale = 1.0f;
	return Scale;
}

// exponential smoothing. rate = 1 - 2^(-speed * dt)
inline float UiAnim(ImGuiID Key, float Target, float Speed = -1.f)
{
	if (Speed < 0.f)
	{
		Speed = Tune().AnimSpeed;
	}
	static std::unordered_map<ImGuiID, float> GValues;
	const float Dt = ImGui::GetIO().DeltaTime;
	float& Value = GValues[Key];
	if (!Tune().bAnimations)
	{
		Value = Target;
		return Value;
	}
	const float Rate = 1.0f - powf(2.0f, -Speed * Dt);
	Value += Rate * (Target - Value);
	if (fabsf(Target - Value) < 0.001f)
	{
		Value = Target;
	}
	return Value;
}

inline float UiAnimStr(const char* Id, float Target, float Speed = -1.f)
{
	return UiAnim(ImGui::GetID(Id), Target, Speed);
}

// stacked rects stand in for edge softness
inline void UiDropShadow(ImDrawList* DrawList, ImVec2 Min, ImVec2 Max, float Rounding, float Alpha = 1.f)
{
	if (Alpha <= 0.001f)
	{
		return;
	}
	const FUiTune& Tuning = Tune();
	const float Dpi = UiDpi();
	const float Offset = Tuning.ShadowOffset * Dpi;
	const int32 Steps = 8;
	for (int32 Step = Steps; Step >= 1; Step--)
	{
		const float Frac = (float)Step / (float)Steps;
		const float Pad = Tuning.ShadowPad * Dpi * Frac;
		const float StepAlpha = Alpha * (0.5f / Steps) * (1.0f - Frac * 0.35f);
		const ImU32 Color = WithAlpha(RGBA(Palette::DropShadow), StepAlpha);
		DrawList->AddRectFilled(ImVec2(Min.x - Pad + Offset, Min.y - Pad + Offset),
			ImVec2(Max.x + Pad + Offset, Max.y + Pad + Offset),
			Color, Rounding + Pad);
	}
}
