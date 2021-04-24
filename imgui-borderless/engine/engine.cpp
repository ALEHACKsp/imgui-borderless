#include "engine.hpp"

#include <stdexcept>
#include <system_error>

#include <d3d9.h>
#pragma comment( lib, "d3d9.lib" )

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include "../imgui/imgui.hpp"
#include "../imgui/imgui_internal.hpp"
#include "../imgui/backend/imgui_impl_dx9.h"
#include "../imgui/backend/imgui_impl_win32.h"

namespace {

	static LPDIRECT3D9              g_pD3D = NULL;
	static LPDIRECT3DDEVICE9        g_pd3dDevice = NULL;
	static D3DPRESENT_PARAMETERS    g_d3dpp = {};

	bool CreateDeviceD3D(HWND hWnd)
	{
		if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == NULL)
			return false;

		ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
		g_d3dpp.Windowed = TRUE;
		g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
		g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
		g_d3dpp.EnableAutoDepthStencil = TRUE;
		g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
		//g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
		g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
		if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
			return false;

		return true;
	}

	void CleanupDeviceD3D()
	{
		if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
		if (g_pD3D) { g_pD3D->Release(); g_pD3D = NULL; }
	}

	void ResetDevice()
	{
		ImGui_ImplDX9_InvalidateDeviceObjects();
		HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
		if (hr == D3DERR_INVALIDCALL)
			IM_ASSERT(0);

		ImGui_ImplDX9_CreateDeviceObjects();
	}

	enum class Style : DWORD {
		windowed = WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
		aero_borderless = WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
		basic_borderless = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX
	};

	auto maximized(HWND hwnd) -> bool {
		WINDOWPLACEMENT placement;
		if (!::GetWindowPlacement(hwnd, &placement)) {
			return false;
		}

		return placement.showCmd == SW_MAXIMIZE;
	}

	auto adjust_maximized_client_rect(HWND window, RECT& rect) -> void {
		if (!maximized(window)) {
			return;
		}

		auto monitor = ::MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
		if (!monitor) {
			return;
		}

		MONITORINFO monitor_info{};
		monitor_info.cbSize = sizeof(monitor_info);
		if (!::GetMonitorInfoW(monitor, &monitor_info)) {
			return;
		}

		rect = monitor_info.rcWork;
	}

	auto last_error(const std::string& message) -> std::system_error {
		return std::system_error(
			std::error_code(::GetLastError(), std::system_category()),
			message
		);
	}

	auto window_class(WNDPROC wndproc) -> const wchar_t* {
		static const wchar_t* window_class_name = [&] {
			WNDCLASSEXW wcx{};
			wcx.cbSize = sizeof(wcx);
			wcx.style = CS_HREDRAW | CS_VREDRAW;
			wcx.hInstance = nullptr;
			wcx.lpfnWndProc = wndproc;
			wcx.lpszClassName = L"BorderlessWindowClass";
			wcx.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
			wcx.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
			const ATOM result = ::RegisterClassExW(&wcx);
			if (!result) {
				throw last_error("failed to register window class");
			}
			return wcx.lpszClassName;
		}();
		return window_class_name;
	}

	auto composition_enabled() -> bool {
		BOOL composition_enabled = FALSE;
		bool success = ::DwmIsCompositionEnabled(&composition_enabled) == S_OK;
		return composition_enabled && success;
	}

	auto select_borderless_style() -> Style {
		return composition_enabled() ? Style::aero_borderless : Style::basic_borderless;
	}

	auto set_shadow(HWND handle, bool enabled) -> void {
		if (composition_enabled()) {
			static const MARGINS shadow_state[2]{ { 0,0,0,0 },{ 1,1,1,1 } };
			::DwmExtendFrameIntoClientArea(handle, &shadow_state[enabled]);
		}
	}

	auto create_window(WNDPROC wndproc, void* userdata, const wchar_t* c_window, int c_size_x, int c_size_y) -> unique_handle {
		HWND hd = GetDesktopWindow(); RECT rect; GetClientRect(hd, &rect);
		int client_width = (rect.right - rect.left);
		int client_height = (rect.bottom - rect.top);

		auto handle = CreateWindowExW(
			0, window_class(wndproc), c_window,
			static_cast<DWORD>(Style::aero_borderless), client_width / 2 - c_size_x / 2, client_height / 2 - c_size_y / 2,
			c_size_x, c_size_y, nullptr, nullptr, nullptr, userdata
		);
		if (!handle) {
			throw last_error("failed to create window");
		}
		return unique_handle{ handle };
	}
}

imgui_borderless::imgui_borderless(const wchar_t* c_window, int window_size_x, int window_size_y, std::function<void()> gui) : handle{ create_window(&imgui_borderless::WndProc, this, c_window, window_size_x, window_size_y) }
{
	ImGui_ImplWin32_EnableDpiAwareness();

	set_borderless(borderless);
	set_borderless_shadow(borderless_shadow);

	::ShowWindow(handle.get(), SW_SHOW);
	::UpdateWindow(handle.get());

	if (!CreateDeviceD3D(handle.get()))
	{
		CleanupDeviceD3D();
		::UnregisterClass(L"BorderlessWindowClass", nullptr);
		return;
	}

	ImGui::CreateContext();

	ImGui_ImplWin32_Init(handle.get());
	ImGui_ImplDX9_Init(g_pd3dDevice);

	MSG msg;
	ZeroMemory(&msg, sizeof(msg));
	while (msg.message != WM_QUIT)
	{
		if (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			continue;
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();

		ImGui::NewFrame();
		{
			gui();
		}
		ImGui::EndFrame();

		g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		D3DCOLOR clear_col_dx = D3DCOLOR_RGBA(123, 255, 255, 255);
		g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
		if (g_pd3dDevice->BeginScene() >= 0)
		{
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			g_pd3dDevice->EndScene();
		}
		HRESULT result = g_pd3dDevice->Present(NULL, NULL, NULL, NULL);

		if (result == D3DERR_DEVICELOST && g_pd3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
			ResetDevice();
	}

	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	exit(0);
}

void imgui_borderless::set_borderless(bool enabled) {
	Style new_style = (enabled) ? select_borderless_style() : Style::windowed;
	Style old_style = static_cast<Style>(::GetWindowLongPtrW(handle.get(), GWL_STYLE));

	if (new_style != old_style) {
		borderless = enabled;

		::SetWindowLongPtrW(handle.get(), GWL_STYLE, static_cast<LONG>(new_style));

		set_shadow(handle.get(), borderless_shadow && (new_style != Style::windowed));

		::SetWindowPos(handle.get(), nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
		::ShowWindow(handle.get(), SW_SHOW);
	}
}

void imgui_borderless::set_borderless_shadow(bool enabled) {
	if (borderless) {
		borderless_shadow = enabled;
		set_shadow(handle.get(), enabled);
	}
}

POINTS m_Pos;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


auto CALLBACK imgui_borderless::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT {
	if (msg == WM_NCCREATE) {
		auto userdata = reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams;
		::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(userdata));
	}

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
		return true;

	if (auto window_ptr = reinterpret_cast<imgui_borderless*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
		auto& window = *window_ptr;

		switch (msg) {

		case WM_LBUTTONDOWN:
		{
			m_Pos = MAKEPOINTS(lparam); // set click points
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			if (wparam == MK_LBUTTON && window.borderless)
			{
				POINTS p = MAKEPOINTS(lparam);
				RECT rect;
				GetWindowRect(FindWindow(L"BorderlessWindowClass", nullptr), &rect);
				rect.left += p.x - m_Pos.x;
				rect.top += p.y - m_Pos.y;
				if (m_Pos.x >= 0 && m_Pos.x <= window.window_size_x && m_Pos.y >= 0 && m_Pos.y <= 19)
					SetWindowPos(FindWindow(L"BorderlessWindowClass", nullptr), HWND_TOPMOST, rect.left, rect.top, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOZORDER);
			}
			return 0;
		}

		case WM_NCCALCSIZE: {
			if (wparam == TRUE && window.borderless) {
				auto& params = *reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
				adjust_maximized_client_rect(hwnd, params.rgrc[0]);
				return 0;
			}
			break;
		}
		case WM_NCHITTEST: {
			if (window.borderless) {

				RECT winrect;
				GetWindowRect(hwnd, &winrect);
				long x = GET_X_LPARAM(lparam);
				long y = GET_Y_LPARAM(lparam);

				// BOTTOM LEFT
				if (x >= winrect.left && x < winrect.left + 5 &&
					y < winrect.bottom && y >= winrect.bottom - 5) {
					return HTBOTTOMLEFT;
				}
				// BOTTOM RIGHT
				if (x < winrect.right && x >= winrect.right - 5 &&
					y < winrect.bottom && y >= winrect.bottom - 5) {
					return HTBOTTOMRIGHT;
				}
				// TOP LEFT
				if (x >= winrect.left && x < winrect.left + 5 &&
					y >= winrect.top && y < winrect.top + 5) {
					return HTTOPLEFT;
				}
				// TOP RIGHT
				if (x < winrect.right && x >= winrect.right - 5 &&
					y >= winrect.top && y < winrect.top + 5) {
					return HTTOPRIGHT;
				}
				// LEFT
				if (x >= winrect.left && x < winrect.left + 5) {
					return HTLEFT;
				}
				// RIGHT
				if (x < winrect.right && x >= winrect.right - 5) {
					return HTRIGHT;
				}
				// BOTTOM
				if (y < winrect.bottom && y >= winrect.bottom - 5) {
					return HTBOTTOM;
				}
				// TOP
				if (y >= winrect.top && y < winrect.top + 5) {
					return HTTOP;
				}
			}
			break;
		}
		case WM_NCACTIVATE: {
			if (!composition_enabled()) {
				return 1;
			}
			break;
		}

		case WM_CLOSE: {
			::DestroyWindow(hwnd);
			return 0;
		}

		case WM_SIZE:
			if (g_pd3dDevice != NULL && wparam != SIZE_MINIMIZED)
			{
				g_d3dpp.BackBufferWidth = LOWORD(lparam);
				g_d3dpp.BackBufferHeight = HIWORD(lparam);
				ResetDevice();
			}
			return 0;

		case WM_DESTROY: {
			PostQuitMessage(0);
			return 0;
		}
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN: {
			switch (wparam) {
			case VK_F8: { window.borderless_drag = !window.borderless_drag;        return 0; }
			case VK_F9: { window.borderless_resize = !window.borderless_resize;    return 0; }
			case VK_F10: { window.set_borderless(!window.borderless);               return 0; }
			case VK_F11: { window.set_borderless_shadow(!window.borderless_shadow); return 0; }
			}
			break;
		}
		}
	}

	return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}