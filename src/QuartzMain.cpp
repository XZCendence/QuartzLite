// window, d3d11 device, frame loop and tailed files
#include "QuartzChrome.h"
#include "QuartzLogStore.h"
#include "QuartzTypes.h"
#include "QuartzFastTail.h"
#include "QuartzLogView.h"
#include "QuartzNotify.h"
#include "QuartzResources.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commdlg.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// win11 chrome. defined locally so the build does not depend on how new the sdk is
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
	ID3D11Device*           GDevice = nullptr;
	ID3D11DeviceContext*    GContext = nullptr;
	IDXGISwapChain*         GSwap = nullptr;
	ID3D11RenderTargetView* GRTV = nullptr;
	UINT                    GResizeW = 0;
	UINT                    GResizeH = 0;
	ImFont*                 GMonoFont = nullptr;
	bool                    GWantsExit = false;
	bool                    GWantsOpenDialog = false;
	HWND                    GHwnd = nullptr;

	// heap-allocated and never destroyed while the app runs. other code holds pointers into them
	std::vector<std::unique_ptr<FLogSource>> GSources;
	std::vector<std::thread> GTailThreads;

	// queued by the wndproc, consumed by the frame loop
	std::mutex GDropMutex;
	std::vector<std::string> GDroppedFiles;

	// panels already considered for default docking. cleared by window > reset layout
	std::unordered_set<std::string> GDockAttempted;

	// last verse compile, for the status bar chip
	char  GBuildStatus[32] = {};
	int32 GBuildTone = 0;

	std::string WideToUtf8(const wchar_t* Wide)
	{
		const int Len = ::WideCharToMultiByte(CP_UTF8, 0, Wide, -1, nullptr, 0, nullptr, nullptr);
		std::string Out(Len > 0 ? Len - 1 : 0, '\0');
		if (Len > 1)
		{
			::WideCharToMultiByte(CP_UTF8, 0, Wide, -1, &Out[0], Len, nullptr, nullptr);
		}
		return Out;
	}

	std::wstring Utf8ToWide(const std::string& Utf8)
	{
		const int Len = ::MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), -1, nullptr, 0);
		std::wstring Out(Len > 0 ? Len - 1 : 0, L'\0');
		if (Len > 1)
		{
			::MultiByteToWideChar(CP_UTF8, 0, Utf8.c_str(), -1, &Out[0], Len);
		}
		return Out;
	}

	void CreateRTV()
	{
		ID3D11Texture2D* Back = nullptr;
		GSwap->GetBuffer(0, IID_PPV_ARGS(&Back));
		if (Back)
		{
			GDevice->CreateRenderTargetView(Back, nullptr, &GRTV);
			Back->Release();
		}
	}

	void CleanupRTV()
	{
		if (GRTV)
		{
			GRTV->Release();
			GRTV = nullptr;
		}
	}

	bool CreateDeviceD3D(HWND Hwnd)
	{
		DXGI_SWAP_CHAIN_DESC SD = {};
		SD.BufferCount = 2;
		SD.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		SD.BufferDesc.RefreshRate.Numerator = 60;
		SD.BufferDesc.RefreshRate.Denominator = 1;
		SD.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		SD.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SD.OutputWindow = Hwnd;
		SD.SampleDesc.Count = 1;
		SD.Windowed = TRUE;
		SD.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		D3D_FEATURE_LEVEL Got;
		D3D_FEATURE_LEVEL Levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
		if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			Levels, 2, D3D11_SDK_VERSION, &SD, &GSwap, &GDevice, &Got, &GContext)))
		{
			return false;
		}
		CreateRTV();
		return true;
	}

	void CleanupDeviceD3D()
	{
		CleanupRTV();
		FBackdropBlur::SafeRelease(GSwap);
		FBackdropBlur::SafeRelease(GContext);
		FBackdropBlur::SafeRelease(GDevice);
	}

	LRESULT WINAPI MainWndProc(HWND Hwnd, UINT Msg, WPARAM WParam, LPARAM LParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(Hwnd, Msg, WParam, LParam))
		{
			return true;
		}
		switch (Msg)
		{
		case WM_SIZE:
			if (WParam == SIZE_MINIMIZED)
			{
				return 0;
			}
			GResizeW = LOWORD(LParam);
			GResizeH = HIWORD(LParam);
			return 0;
		case WM_SYSCOMMAND:
			if ((WParam & 0xfff0) == SC_KEYMENU)
			{
				return 0;
			}
			break;
		case WM_DPICHANGED:
		{
			const RECT* Rect = (const RECT*)LParam;
			::SetWindowPos(Hwnd, nullptr, Rect->left, Rect->top,
				Rect->right - Rect->left, Rect->bottom - Rect->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			return 0;
		}
		case WM_DROPFILES:
		{
			HDROP Drop = (HDROP)WParam;
			const UINT FileCount = ::DragQueryFileW(Drop, 0xFFFFFFFF, nullptr, 0);
			for (UINT Index = 0; Index < FileCount; ++Index)
			{
				wchar_t Path[MAX_PATH];
				if (::DragQueryFileW(Drop, Index, Path, MAX_PATH))
				{
					std::lock_guard<std::mutex> Lock(GDropMutex);
					GDroppedFiles.push_back(WideToUtf8(Path));
				}
			}
			::DragFinish(Drop);
			return 0;
		}
		case WM_CLOSE:
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
		default:
			break;
		}
		return ::DefWindowProcW(Hwnd, Msg, WParam, LParam);
	}

	// %LOCALAPPDATA%/<App>/Saved/Logs/<App>.log. empty when LOCALAPPDATA is not set
	std::string BuildLogPath(const char* AppName)
	{
		wchar_t Buf[MAX_PATH];
		const DWORD Len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", Buf, MAX_PATH);
		if (Len == 0 || Len >= MAX_PATH)
		{
			return std::string();
		}
		return WideToUtf8(Buf) + "\\" + AppName + "\\Saved\\Logs\\" + AppName + ".log";
	}

	bool FileExists(const std::string& Path)
	{
		const DWORD Attrs = ::GetFileAttributesW(Utf8ToWide(Path).c_str());
		return Attrs != INVALID_FILE_ATTRIBUTES && !(Attrs & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::string LeafName(const std::string& Path)
	{
		const size_t Slash = Path.find_last_of("/\\");
		return Slash == std::string::npos ? Path : Path.substr(Slash + 1);
	}

	// watch live lines for a finished verse compile
	void CheckCompileEvents()
	{
		struct FScanState
		{
			int64 NextLine = 0;
			bool bPrimed = false;
		};
		static std::unordered_map<const FLogSource*, FScanState> ScanStates;
		static std::string PendingError;    // first diagnostic of the current compile
		static double PendingCompileMs = -1.0;
		static double LastNotifyAt = 0.0;

		// "C:/long/path/foo.verse(15,1, 15,28): Script error 3532: ..." -> short form
		auto CondenseError = [](const std::string& In)
		{
			const size_t Paren = In.find('(');
			std::string Head = Paren == std::string::npos ? In : In.substr(0, Paren);
			const std::string Tail = Paren == std::string::npos ? std::string() : In.substr(Paren);
			const size_t Slash = Head.find_last_of("/\\");
			if (Slash != std::string::npos)
			{
				Head = Head.substr(Slash + 1);
			}
			std::string Out = Head + Tail;
			if (Out.size() > 150)
			{
				Out = Out.substr(0, 147) + "...";
			}
			return Out;
		};

		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			FScanState& Scan = ScanStates[Source.get()];
			const std::vector<FLogLine>& Items = Source->GetItems();
			const int64 Base = Source->GetDroppedCount();
			const int64 End = Base + (int64)Items.size();

			// first pass adopts whatever was already buffered without firing
			if (!Scan.bPrimed)
			{
				Scan.NextLine = End;
				Scan.bPrimed = true;
				continue;
			}

			for (int64 Id = ImMax(Scan.NextLine, Base); Id < End; ++Id)
			{
				const FLogLine& Line = Items[(size_t)(Id - Base)];

				const bool bBuild = Line.Category == "VerseBuild";
				const bool bCompiler = Line.Category == "LogSolLoadCompiler";
				if (!bBuild && !bCompiler)
				{
					continue;
				}

				if (bBuild && PendingError.empty()
					&& Line.Text.find("Script error") != std::string::npos)
				{
					PendingError = Line.Text;   // first error is the one worth showing
				}

				// versebuild completion is the terminal event. logsolloadcompiler FAILED is a backstop
				bool bFinished = false;
				bool bSucceeded = false;
				if (bCompiler && Line.Text.find("Global Verse compile") != std::string::npos)
				{
					const size_t At = Line.Text.find("compiled in ");
					if (At != std::string::npos)
					{
						PendingCompileMs = atof(Line.Text.c_str() + At + 12);
					}
					if (Line.Text.find("finished: FAILED") != std::string::npos)
					{
						bFinished = true;
					}
				}
				else if (bBuild && Line.Text.find("Build complete") != std::string::npos)
				{
					bFinished = true;
					bSucceeded = Line.Text.find("SUCCESS") != std::string::npos;
				}

				if (bFinished)
				{
					// both signals can fire for one compile. collapse anything within a couple of seconds
					const double Now = ImGui::GetTime();
					if (Now - LastNotifyAt > 2.0)
					{
						LastNotifyAt = Now;
						// the title already carries the verdict. the body only says what
						// the title cannot: timing and the first error. empty stays empty,
						// so a bare result renders as a single-line toast
						std::string Body;
						if (PendingCompileMs > 0.0)
						{
							char Took[32];
							snprintf(Took, sizeof(Took), "Took %.1fs", PendingCompileMs / 1000.0);
							Body = Took;
						}
						if (!bSucceeded && !PendingError.empty())
						{
							if (!Body.empty())
							{
								Body += "\n";
							}
							Body += CondenseError(PendingError);
						}
						QuartzNotify::Push(
							bSucceeded ? "Verse compile succeeded" : "Verse compile failed",
							Body.c_str(),
							bSucceeded ? QuartzNotify::ESeverity::Success : QuartzNotify::ESeverity::Failure);

						snprintf(GBuildStatus, sizeof(GBuildStatus), "%s",
							bSucceeded ? "Verse OK" : "build failed");
						GBuildTone = bSucceeded ? 1 : 3;
					}
					PendingError.clear();
					PendingCompileMs = -1.0;
				}
			}
			Scan.NextLine = End;
		}
	}

	void OpenFileDialog()
	{
		wchar_t Path[MAX_PATH] = {};
		OPENFILENAMEW Ofn = {};
		Ofn.lStructSize = sizeof(Ofn);
		Ofn.hwndOwner = GHwnd;
		Ofn.lpstrFilter = L"Log files (*.log;*.txt)\0*.log;*.txt\0All files (*.*)\0*.*\0";
		Ofn.lpstrFile = Path;
		Ofn.nMaxFile = MAX_PATH;
		Ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		if (::GetOpenFileNameW(&Ofn))
		{
			QuartzUI::HostOpenFile(WideToUtf8(Path).c_str());
		}
	}
}

// host hooks for the chrome

namespace QuartzUI
{
	int32 GetOpenFileCount()
	{
		int32 Count = 0;
		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			if (Source->bOpen)
			{
				++Count;
			}
		}
		return Count;
	}

	const char* GetOpenFilePath(int32 Index)
	{
		int32 Count = 0;
		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			if (Source->bOpen && Count++ == Index)
			{
				return Source->Path.c_str();
			}
		}
		return "";
	}

	bool HostFileExists(const char* Path)
	{
		return FileExists(Path);
	}

	const char* GetEditorLogPath()
	{
		static const std::string Path = BuildLogPath("UnrealEditorFortnite");
		return Path.c_str();
	}

	const char* GetGameLogPath()
	{
		static const std::string Path = BuildLogPath("FortniteGame");
		return Path.c_str();
	}

	void HostOpenFile(const char* Path)
	{
		if (!Path || !Path[0])
		{
			return;
		}
		// absolute so a relative path from the command line does not dodge the dedup below
		std::string Absolute = Path;
		wchar_t Full[MAX_PATH];
		if (::GetFullPathNameW(Utf8ToWide(Path).c_str(), MAX_PATH, Full, nullptr))
		{
			Absolute = WideToUtf8(Full);
		}
		NoteRecentFile(Absolute.c_str());
		// already tailed. just make the window visible again
		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			if (Source->Path == Absolute)
			{
				Source->bOpen = true;
				return;
			}
		}

		std::unique_ptr<FLogSource> Source = std::make_unique<FLogSource>();
		Source->Path = Absolute;
		Source->Name = LeafName(Source->Path);
		// window titles must be unique or imgui folds them into one
		int32 Suffix = 2;
		for (size_t Index = 0; Index < GSources.size();)
		{
			if (GSources[Index]->Name == Source->Name)
			{
				Source->Name = LeafName(Source->Path) + " (" + std::to_string(Suffix++) + ")";
				Index = 0;
				continue;
			}
			++Index;
		}
		GTailThreads.push_back(StartTail(Source.get()));
		GSources.push_back(std::move(Source));
		ImGui::MarkIniSettingsDirty();
	}

	void HostOpenFileDialog()
	{
		// deferred. the dialog is modal and must not block between NewFrame and Render
		GWantsOpenDialog = true;
	}

	void HostRequestExit()
	{
		GWantsExit = true;
	}

	void ForgetDockedWindows()
	{
		GDockAttempted.clear();
	}
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR, int)
{
	// per-monitor dpi must be set before the window exists
	ImGui_ImplWin32_EnableDpiAwareness();

	WNDCLASSEXW WC = { sizeof(WC), CS_CLASSDC, MainWndProc, 0L, 0L, Instance,
		::LoadIconW(nullptr, IDI_APPLICATION), nullptr, nullptr, nullptr, L"QuartzLog",
		::LoadIconW(nullptr, IDI_APPLICATION) };
	::RegisterClassExW(&WC);

	GHwnd = ::CreateWindowW(WC.lpszClassName, L"Quartz",
		WS_OVERLAPPEDWINDOW, 80, 60, 1500, 900, nullptr, nullptr, Instance, nullptr);
	if (!GHwnd)
	{
		return 1;
	}

	const BOOL bDark = TRUE;
	::DwmSetWindowAttribute(GHwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark));
	const DWORD Backdrop = DWMSBT_MAINWINDOW;
	::DwmSetWindowAttribute(GHwnd, DWMWA_SYSTEMBACKDROP_TYPE, &Backdrop, sizeof(Backdrop));
	const DWORD Corners = DWMWCP_ROUND;
	::DwmSetWindowAttribute(GHwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &Corners, sizeof(Corners));

	if (!CreateDeviceD3D(GHwnd))
	{
		CleanupDeviceD3D();
		::DestroyWindow(GHwnd);
		return 1;
	}

	::DragAcceptFiles(GHwnd, TRUE);
	::ShowWindow(GHwnd, SW_SHOWDEFAULT);
	::UpdateWindow(GHwnd);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	IO.ConfigDpiScaleFonts = true;   // rasterize glyphs at the monitor's real dpi
	// real os windows for torn-off panels and toasts. must be set before the backends init
	IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	IO.ConfigViewportsNoTaskBarIcon = true;

	// %LOCALAPPDATA%/Quartz. absolute because imgui fopen is relative to a cwd we do not control
	static std::string IniPath;
	{
		wchar_t Buf[MAX_PATH];
		if (::GetEnvironmentVariableW(L"LOCALAPPDATA", Buf, MAX_PATH))
		{
			const std::wstring Dir = std::wstring(Buf) + L"\\Quartz";
			::CreateDirectoryW(Dir.c_str(), nullptr);
			IniPath = WideToUtf8(Dir.c_str()) + "\\quartz.ini";
			IO.IniFilename = IniPath.c_str();
		}
	}
	RegisterLogViewSettings();
	QuartzUI::RegisterChromeSettingsHandler();

	// segoe ui for chrome. jetbrains mono is baked into the exe as a resource so a bare
	// download of Quartz.exe is self-contained
	{
		ImFont* UiFont = nullptr;
		wchar_t WinBuf[MAX_PATH];
		if (::GetWindowsDirectoryW(WinBuf, MAX_PATH))
		{
			const std::string SegoePath = WideToUtf8(WinBuf) + "\\Fonts\\segoeui.ttf";
			if (FileExists(SegoePath))
			{
				UiFont = IO.Fonts->AddFontFromFileTTF(SegoePath.c_str(), 17.0f);
			}
		}
		if (!UiFont)
		{
			IO.Fonts->AddFontDefault();
		}

		if (HRSRC Resource = ::FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_FONT_MONO), (LPWSTR)RT_RCDATA))
		{
			if (HGLOBAL Handle = ::LoadResource(nullptr, Resource))
			{
				void* Data = ::LockResource(Handle);
				const DWORD Size = ::SizeofResource(nullptr, Resource);
				if (Data && Size > 0)
				{
					// resource memory lives as long as the process. the atlas must not
					// take ownership or it would free a pointer it cannot free
					ImFontConfig Config;
					Config.FontDataOwnedByAtlas = false;
					GMonoFont = IO.Fonts->AddFontFromMemoryTTF(Data, (int32)Size, 15.0f, &Config);
				}
			}
		}
		if (!GMonoFont)
		{
			GMonoFont = UiFont;   // better the ui face than the imgui bitmap font
		}
		SetLogViewMonoFont(GMonoFont);
	}

	QuartzUI::ApplyTheme(ImGui_ImplWin32_GetDpiScaleForHwnd(GHwnd));

	ImGui_ImplWin32_Init(GHwnd);
	ImGui_ImplDX11_Init(GDevice, GContext);
	QuartzUI::InitChrome(GDevice, GContext);

	bool bFirstFrame = true;
	while (!GWantsExit)
	{
		MSG Msg;
		while (::PeekMessage(&Msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&Msg);
			::DispatchMessage(&Msg);
			if (Msg.message == WM_QUIT)
			{
				GWantsExit = true;
			}
		}
		if (GWantsExit)
		{
			break;
		}

		if (GResizeW && GResizeH)
		{
			CleanupRTV();
			GSwap->ResizeBuffers(0, GResizeW, GResizeH, DXGI_FORMAT_UNKNOWN, 0);
			GResizeW = GResizeH = 0;
			CreateRTV();
		}

		// re-apply from scratch when dpi changes so sizes do not compound
		const float NowDpi = ImGui_ImplWin32_GetDpiScaleForHwnd(GHwnd);
		if (fabsf(NowDpi - UiDpi()) > 0.01f)
		{
			QuartzUI::ApplyTheme(NowDpi);
		}

		RECT ClientRect;
		::GetClientRect(GHwnd, &ClientRect);
		QuartzUI::ResizeBlur(ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top);

		{
			std::vector<std::string> Dropped;
			{
				std::lock_guard<std::mutex> Lock(GDropMutex);
				Dropped.swap(GDroppedFiles);
			}
			for (const std::string& Path : Dropped)
			{
				QuartzUI::HostOpenFile(Path.c_str());
			}
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// ini loads during the first NewFrame. a fresh install opens the uefn editor log
		if (bFirstFrame)
		{
			bFirstFrame = false;
			// command line files open regardless of what was restored
			int ArgCount = 0;
			if (LPWSTR* Args = ::CommandLineToArgvW(::GetCommandLineW(), &ArgCount))
			{
				for (int Index = 1; Index < ArgCount; ++Index)
				{
					QuartzUI::HostOpenFile(WideToUtf8(Args[Index]).c_str());
				}
				::LocalFree(Args);
			}
			if (!QuartzUI::ChromeSettingsWereLoaded() && GSources.empty())
			{
				const char* Editor = QuartzUI::GetEditorLogPath();
				if (Editor[0] && FileExists(Editor))
				{
					QuartzUI::HostOpenFile(Editor);
				}
			}
		}

		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O))
		{
			GWantsOpenDialog = true;
		}

		const double Now = ImGui::GetTime();
		int64 TotalLines = 0;
		int32 WatchedFiles = 0;
		std::string TailStatus;
		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			Source->bCollecting.store(Source->bOpen, std::memory_order_relaxed);
			if (Source->bOpen)
			{
				Source->Drain(Now);
				++WatchedFiles;
				TailStatus = Source->GetStatus();
			}
			TotalLines += (int64)Source->GetItems().size();
		}

		QuartzUI::BeginFrameChrome();
		QuartzUI::DrawMenuBar();
		const ImGuiID DockId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
		// dock into the main dockspace only if the ini has no saved entry for it
		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			if (GDockAttempted.insert(Source->Name).second
				&& !ImGui::FindWindowSettingsByID(ImHashStr(Source->Name.c_str())))
			{
				ImGui::DockBuilderDockWindow(Source->Name.c_str(), DockId);
			}
		}

		for (const std::unique_ptr<FLogSource>& Source : GSources)
		{
			if (Source->bOpen)
			{
				DrawLogWindow(*Source);
			}
		}

		CheckCompileEvents();
		QuartzNotify::Draw();

		QuartzUI::FChromeState Chrome;
		Chrome.WatchedFiles = WatchedFiles;
		Chrome.LogLines = TotalLines;
		Chrome.TailStatus = TailStatus.c_str();
		Chrome.Build = GBuildStatus;
		Chrome.BuildTone = GBuildTone;
		QuartzUI::DrawStatusBar(Chrome);
		QuartzUI::EndFrameChrome();

		ImGui::Render();
		const float Clear[4] = { 0.094f, 0.094f, 0.094f, 1.0f };
		GContext->OMSetRenderTargets(1, &GRTV, nullptr);
		GContext->ClearRenderTargetView(GRTV, Clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		QuartzUI::CaptureBlurIfIdle(GSwap);
		GSwap->Present(1, 0);   // vsync paces the loop

		// after present so a frame is never left half-built
		if (GWantsOpenDialog)
		{
			GWantsOpenDialog = false;
			OpenFileDialog();
		}
	}

	// stop the tailers before the sources they point into go away
	for (const std::unique_ptr<FLogSource>& Source : GSources)
	{
		Source->bStop.store(true);
	}
	for (std::thread& Thread : GTailThreads)
	{
		Thread.join();
	}

	QuartzUI::ShutdownChrome();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	CleanupDeviceD3D();
	::DestroyWindow(GHwnd);
	::UnregisterClassW(WC.lpszClassName, Instance);
	return 0;
}
