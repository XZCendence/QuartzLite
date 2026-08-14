cmake_minimum_required(VERSION 3.20)
project(Quartz CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(Quartz WIN32
	src/QuartzMain.cpp
	src/QuartzChrome.cpp
	src/QuartzFastTail.cpp
	src/QuartzLogView.cpp
	src/QuartzNotify.cpp
	src/QuartzResources.rc
	third_party/imgui/imgui.cpp
	third_party/imgui/imgui_draw.cpp
	third_party/imgui/imgui_tables.cpp
	third_party/imgui/imgui_widgets.cpp
	third_party/imgui/imgui_demo.cpp
	third_party/imgui/imgui_impl_win32.cpp
	third_party/imgui/imgui_impl_dx11.cpp
)

target_include_directories(Quartz PRIVATE src third_party/imgui)
target_compile_definitions(Quartz PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Quartz PRIVATE d3d11 d3dcompiler dxgi dwmapi shell32 comdlg32)

if(MSVC)
	target_compile_options(Quartz PRIVATE /W4 /utf-8)
endif()
