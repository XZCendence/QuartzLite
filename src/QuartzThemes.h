#pragma once

#include "QuartzTypes.h"

#include "imgui.h"

// append only. the ini stores the selection by name. the menu groups by these boundaries
enum class EAppTheme : uint8
{
	ImGuiDark,
	ImGuiClassic,
	ImGuiLight,
	DeepDark,
	CatppuccinMocha,
	Nord,
	// community imgui styles
	Moonlight,
	ModernDark,
	Cinder,
	EnemyMouse,
	Photoshop,
	// editor palettes
	Dracula,
	GruvboxDark,
	TokyoNight,
	TokyoNightStorm,
	RosePine,
	RosePineDawn,
	SolarizedDark,
	SolarizedLight,
	OneDark,
	Monokai,
	Everforest,
	Kanagawa,
	AyuDark,
	AyuMirage,
	MaterialOcean,
	Cobalt2,
	NightOwl,
	CatppuccinLatte,
	CatppuccinFrappe,
	CatppuccinMacchiato,
	Count,
};

// palette themes need the seven colours. imgui presets carry a null palette
struct FAppThemeDef
{
	const char* Name;
	bool        bPalette;    // false => handled in ApplyAppTheme
	bool        bLightBase;  // start from StyleColorsLight for a light bg
	uint32      Bg;
	uint32      Surface;
	uint32      Raised;
	uint32      Text;
	uint32      Muted;
	uint32      Accent;
	uint32      Border;
};

inline const FAppThemeDef* AppThemeTable()
{
	// published colours for each scheme
	static const FAppThemeDef Table[] =
	{
		{ "ImGui Dark (built in)",    false, false, 0, 0, 0, 0, 0, 0, 0 },
		{ "ImGui Classic (built in)", false, false, 0, 0, 0, 0, 0, 0, 0 },
		{ "ImGui Light (built in)",   false, true,  0, 0, 0, 0, 0, 0, 0 },
		{ "Deep Dark",          true,  false, 0x101010, 0x181818, 0x292929, 0xffffff, 0x8a8a8a, 0x00a8ff, 0x303030 },
		{ "Catppuccin Mocha",   true,  false, 0x1e1e2e, 0x181825, 0x313244, 0xcdd6f4, 0x7f849c, 0x89b4fa, 0x45475a },
		{ "Nord",               true,  false, 0x2e3440, 0x3b4252, 0x434c5e, 0xeceff4, 0x9aa4b3, 0x88c0d0, 0x4c566a },

		// community imgui styles
		// moonlight, madam-herta. near-black navy, citron accent
		{ "Moonlight",          true,  false, 0x14161a, 0x17191d, 0x1c2027, 0xffffff, 0x465173, 0xf8ff7f, 0x282b31 },
		// modern dark, adamharris. greys, warm ember
		{ "Modern Dark",        true,  false, 0x171717, 0x1c1c1c, 0x2e2e2e, 0xffffff, 0x565656, 0xd65c0d, 0x2a2a2a },
		// cinder, simongeilfus. slate blue, crimson
		{ "Cinder",             true,  false, 0x212429, 0x1a1c21, 0x333845, 0xdbede3, 0x6b7280, 0xeb2e4a, 0x2b2f38 },
		// enemymouse. black, cyan
		{ "Enemymouse",         true,  false, 0x000000, 0x001a1a, 0x004d4d, 0x00ffff, 0x006668, 0x00e5e5, 0x00cccc },
		{ "Photoshop",          true,  false, 0x212121, 0x2b2b2b, 0x3f3f3f, 0xd8d8d8, 0x808080, 0x4b8bf5, 0x3f3f3f },

		// editor palettes
		{ "Dracula",            true,  false, 0x282a36, 0x21222c, 0x44475a, 0xf8f8f2, 0x6272a4, 0xbd93f9, 0x44475a },
		{ "Gruvbox Dark",       true,  false, 0x282828, 0x1d2021, 0x504945, 0xebdbb2, 0x928374, 0xfe8019, 0x3c3836 },
		{ "Tokyo Night",        true,  false, 0x1a1b26, 0x16161e, 0x292e42, 0xc0caf5, 0x565f89, 0x7aa2f7, 0x3b4261 },
		{ "Tokyo Night Storm",  true,  false, 0x24283b, 0x1f2335, 0x2f334d, 0xc0caf5, 0x565f89, 0x7aa2f7, 0x3b4261 },
		{ "Rose Pine",          true,  false, 0x191724, 0x1f1d2e, 0x26233a, 0xe0def4, 0x6e6a86, 0xc4a7e7, 0x403d52 },
		{ "Rose Pine Dawn",     true,  true,  0xfaf4ed, 0xfffaf3, 0xf2e9e1, 0x575279, 0x9893a5, 0x907aa9, 0xdfdad9 },
		{ "Solarized Dark",     true,  false, 0x002b36, 0x073642, 0x0f4b59, 0x93a1a1, 0x586e75, 0x268bd2, 0x094352 },
		{ "Solarized Light",    true,  true,  0xfdf6e3, 0xeee8d5, 0xe0dac4, 0x586e75, 0x93a1a1, 0x268bd2, 0xd6cfb8 },
		{ "One Dark",           true,  false, 0x282c34, 0x21252b, 0x3e4451, 0xabb2bf, 0x5c6370, 0x61afef, 0x3e4451 },
		{ "Monokai",            true,  false, 0x272822, 0x1e1f1c, 0x3e3d32, 0xf8f8f2, 0x75715e, 0xa6e22e, 0x49483e },
		{ "Everforest Dark",    true,  false, 0x2d353b, 0x343f44, 0x3d484d, 0xd3c6aa, 0x859289, 0xa7c080, 0x475258 },
		{ "Kanagawa",           true,  false, 0x1f1f28, 0x16161d, 0x363646, 0xdcd7ba, 0x727169, 0x7e9cd8, 0x54546d },
		{ "Ayu Dark",           true,  false, 0x0b0e14, 0x0f131a, 0x1b1f2b, 0xbfbdb6, 0x565b66, 0xe6b450, 0x1f2430 },
		{ "Ayu Mirage",         true,  false, 0x1f2430, 0x1a1f29, 0x2d3444, 0xcccac2, 0x707a8c, 0xffcc66, 0x343b49 },
		{ "Material Ocean",     true,  false, 0x0f111a, 0x1a1c25, 0x232635, 0xa6accd, 0x4b526d, 0x82aaff, 0x2a2e3f },
		{ "Cobalt2",            true,  false, 0x193549, 0x15232d, 0x1f4662, 0xffffff, 0x7f8c98, 0xffc600, 0x234e6d },
		{ "Night Owl",          true,  false, 0x011627, 0x01121f, 0x1d3b53, 0xd6deeb, 0x637777, 0x82aaff, 0x1d3b53 },
		{ "Catppuccin Latte",   true,  true,  0xeff1f5, 0xe6e9ef, 0xccd0da, 0x4c4f69, 0x8c8fa1, 0x1e66f5, 0xbcc0cc },
		{ "Catppuccin Frappe",  true,  false, 0x303446, 0x292c3c, 0x414559, 0xc6d0f5, 0x838ba7, 0x8caaee, 0x51576d },
		{ "Catppuccin Macchiato", true, false, 0x24273a, 0x1e2030, 0x363a4f, 0xcad3f5, 0x6e738d, 0x8aadf4, 0x494d64 },
	};
	static_assert(sizeof(Table) / sizeof(Table[0]) == (size_t)EAppTheme::Count,
		"EAppTheme enum and AppThemeTable must stay in step");
	return Table;
}

inline const char* AppThemeName(EAppTheme Theme)
{
	return AppThemeTable()[(int32)Theme].Name;
}

// name to enum. false if unknown
inline bool AppThemeFromName(const char* Name, EAppTheme& OutTheme)
{
	for (int32 Index = 0; Index < (int32)EAppTheme::Count; ++Index)
	{
		const char* Candidate = AppThemeTable()[Index].Name;
		int32 At = 0;
		while (Candidate[At] && Name[At] && Candidate[At] == Name[At])
		{
			++At;
		}
		if (Candidate[At] == 0 && (Name[At] == 0 || Name[At] == '\n' || Name[At] == '\r'))
		{
			OutTheme = (EAppTheme)Index;
			return true;
		}
	}
	return false;
}

inline ImVec4 ThemeHex(uint32 Rgb, float Alpha = 1.0f)
{
	return ImVec4(((Rgb >> 16) & 255) / 255.0f, ((Rgb >> 8) & 255) / 255.0f,
		(Rgb & 255) / 255.0f, Alpha);
}

inline void ApplyThemePalette(uint32 Bg, uint32 Surface, uint32 Raised, uint32 Text,
	uint32 Muted, uint32 Accent, uint32 Border)
{
	ImVec4* Colors = ImGui::GetStyle().Colors;
	Colors[ImGuiCol_Text] = ThemeHex(Text);
	Colors[ImGuiCol_TextDisabled] = ThemeHex(Muted);
	Colors[ImGuiCol_WindowBg] = ThemeHex(Bg);
	Colors[ImGuiCol_ChildBg] = ThemeHex(Bg);
	Colors[ImGuiCol_PopupBg] = ThemeHex(Surface, .98f);
	Colors[ImGuiCol_Border] = ThemeHex(Border);
	Colors[ImGuiCol_FrameBg] = ThemeHex(Surface);
	Colors[ImGuiCol_FrameBgHovered] = ThemeHex(Raised);
	Colors[ImGuiCol_FrameBgActive] = ThemeHex(Raised);
	Colors[ImGuiCol_TitleBg] = ThemeHex(Surface);
	Colors[ImGuiCol_TitleBgActive] = ThemeHex(Raised);
	Colors[ImGuiCol_TitleBgCollapsed] = ThemeHex(Surface);
	Colors[ImGuiCol_MenuBarBg] = ThemeHex(Surface);
	Colors[ImGuiCol_ScrollbarBg] = ThemeHex(Bg);
	Colors[ImGuiCol_ScrollbarGrab] = ThemeHex(Raised);
	Colors[ImGuiCol_ScrollbarGrabHovered] = ThemeHex(Muted);
	Colors[ImGuiCol_ScrollbarGrabActive] = ThemeHex(Accent);
	Colors[ImGuiCol_CheckMark] = ThemeHex(Accent);
	Colors[ImGuiCol_SliderGrab] = ThemeHex(Muted);
	Colors[ImGuiCol_SliderGrabActive] = ThemeHex(Accent);
	Colors[ImGuiCol_Button] = ThemeHex(Surface);
	Colors[ImGuiCol_ButtonHovered] = ThemeHex(Raised);
	Colors[ImGuiCol_ButtonActive] = ThemeHex(Accent, .75f);
	Colors[ImGuiCol_Header] = ThemeHex(Raised);
	Colors[ImGuiCol_HeaderHovered] = ThemeHex(Accent, .35f);
	Colors[ImGuiCol_HeaderActive] = ThemeHex(Accent, .55f);
	Colors[ImGuiCol_Separator] = ThemeHex(Border);
	Colors[ImGuiCol_SeparatorHovered] = ThemeHex(Accent, .7f);
	Colors[ImGuiCol_SeparatorActive] = ThemeHex(Accent);
	Colors[ImGuiCol_ResizeGrip] = ThemeHex(Accent, .2f);
	Colors[ImGuiCol_ResizeGripHovered] = ThemeHex(Accent, .55f);
	Colors[ImGuiCol_ResizeGripActive] = ThemeHex(Accent);
	Colors[ImGuiCol_Tab] = ThemeHex(Surface);
	Colors[ImGuiCol_TabHovered] = ThemeHex(Raised);
	Colors[ImGuiCol_TabSelected] = ThemeHex(Raised);
	Colors[ImGuiCol_TabSelectedOverline] = ThemeHex(Accent);
	Colors[ImGuiCol_TabDimmed] = ThemeHex(Bg);
	Colors[ImGuiCol_TabDimmedSelected] = ThemeHex(Surface);
	Colors[ImGuiCol_DockingPreview] = ThemeHex(Accent, .55f);
	Colors[ImGuiCol_DockingEmptyBg] = ThemeHex(Bg);
	Colors[ImGuiCol_TableHeaderBg] = ThemeHex(Surface);
	Colors[ImGuiCol_TableBorderStrong] = ThemeHex(Border);
	Colors[ImGuiCol_TableBorderLight] = ThemeHex(Border, .55f);
	Colors[ImGuiCol_TableRowBg] = ThemeHex(Bg);
	Colors[ImGuiCol_TableRowBgAlt] = ThemeHex(Surface, .55f);
	Colors[ImGuiCol_TextSelectedBg] = ThemeHex(Accent, .35f);
	Colors[ImGuiCol_NavCursor] = ThemeHex(Accent);
}

inline void ApplyAppTheme(EAppTheme Theme)
{
	ImGui::GetStyle() = ImGuiStyle(); // hard reset so presets do not leak
	if (Theme == EAppTheme::ImGuiClassic)
	{
		ImGui::StyleColorsClassic();
		return;
	}
	if (Theme == EAppTheme::ImGuiLight)
	{
		ImGui::StyleColorsLight();
		return;
	}
	const FAppThemeDef& Def = AppThemeTable()[(int32)Theme];
	// applythemepalette does not name every colour. leftovers start from the matching builtin
	if (Def.bLightBase)
	{
		ImGui::StyleColorsLight();
	}
	else
	{
		ImGui::StyleColorsDark();
	}
	if (Def.bPalette)
	{
		ApplyThemePalette(Def.Bg, Def.Surface, Def.Raised, Def.Text, Def.Muted, Def.Accent, Def.Border);
	}
}
