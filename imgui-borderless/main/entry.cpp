#include <stdexcept>

#include "../engine/engine.hpp"
#include "../imgui/imgui.hpp"

void test_interface()
{
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	ImGui::SetNextWindowPos({ 0,0 });
	ImGui::Begin("123", nullptr, ImGuiWindowFlags_NoMove);
	ImGui::End();
}

int main() {
	imgui_borderless window(L"ImGui", 760, 480, test_interface);
}
