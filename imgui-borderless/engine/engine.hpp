#pragma once

#include <memory>
#include <string>

#include <Windows.h>
#include <functional>

struct hwnd_deleter {
	using pointer = HWND;
	auto operator()(HWND handle) const -> void {
		::DestroyWindow(handle);
	}
};

using unique_handle = std::unique_ptr<HWND, hwnd_deleter>;

class imgui_borderless {
public:
	/*
	@ window_name -> Window Titlebar Name
	@ window_size_x -> Window Width
	@ window_size_y -> Window Height
	*/
	imgui_borderless(const wchar_t* window_name, int window_size_x, int window_size_y, std::function<void()> gui);

	auto set_borderless(bool enabled) -> void;
	auto set_borderless_shadow(bool enabled) -> void;

private:
	bool borderless = true;
	bool borderless_resize = true;
	bool borderless_drag = true;
	bool borderless_shadow = true;

	const wchar_t* window_name;
	int window_size_x, window_size_y;

	unique_handle handle;

	static auto CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT;
	auto hit_test(POINT cursor) const->LRESULT;

};
