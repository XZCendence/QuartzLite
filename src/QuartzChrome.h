#pragma once

// theme, glass, tabs, menu bar and status bar

#include "QuartzTypes.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "QuartzStyle.h"
#include "QuartzThemes.h"
#include "QuartzBlur.h"

namespace QuartzUI
{
	// host fills this each frame. empty strings and negative counts hide their cells
	struct FChromeState
	{
		int32     WatchedFiles = 0;    // sources currently collecting
		int64   LogLines = 0;        // lines buffered across every source
		const char* TailStatus = "";     // most recent tailer status
		const char* Build = "";          // last verse compile result
		int32     BuildTone = 0;       // 0 neutral, 1 ok, 2 warn, 3 bad
	};

	void InitChrome(ID3D11Device* Device, ID3D11DeviceContext* Context);
	void ShutdownChrome();

	// host draws its own panels between begin and end
	void BeginFrameChrome();
	void DrawMenuBar();
	void DrawStatusBar(const FChromeState& State);
	void EndFrameChrome();

	// after present, and only on frames where no glass was drawn
	void CaptureBlurIfIdle(IDXGISwapChain* Swap);

	void ResizeBlur(int32 Width, int32 Height);

	void ApplyTheme(float Dpi);

	// theme, accent, notifications and the open file list. call before the first NewFrame
	void RegisterChromeSettingsHandler();

	// true if the ini had a chrome section
	bool ChromeSettingsWereLoaded();

	void NoteRecentFile(const char* Path);

	int32       GetOpenFileCount();
	const char* GetOpenFilePath(int32 Index);
	bool        HostFileExists(const char* Path);
	const char* GetEditorLogPath();                // "" when unavailable
	const char* GetGameLogPath();
	void        HostOpenFile(const char* Path);
	void        HostOpenFileDialog();
	void        HostRequestExit();
	void        ForgetDockedWindows();             // so reset layout can re-dock defaults
}
