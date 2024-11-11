#include "PluginManager.h"
#include "sample/Sample.h"
#include "autosort/autosort.h"
#include "transmute/transmute.h"
#include <imgui.h>

PluginManager::PluginManager() {
    m_Plugins.push_back(new Sample());
    m_Plugins.push_back(new AutoSort());
    m_Plugins.push_back(new Transmute());
}

void PluginManager::Present() {
	for (auto& plugin : m_Plugins) {
		plugin->OnDraw();
	}



	//simple menu
	/*
	if (m_ShowSettings) {
		ImGui::SetNextWindowPos({ 0.f, 0.f }, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize({ 500.f, 300.f }, ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Settings", &m_ShowSettings)) {
			ImGui::Text("Hello World");
		}
		ImGui::End();
	}
	*/
}

//plugin/PluginManager.cpp
void PluginManager::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        for (auto& plugin : m_Plugins) {
            if (plugin->OnKeyPressed(wParam)) {
                break;
            }
        }
        break;
    case WM_KEYUP:
        if (wParam == VK_DELETE) {
            m_ShowSettings = !m_ShowSettings;
        }
        for (auto& plugin : m_Plugins) {
            plugin->OnKeyReleased(wParam);
        }
        break;
    default:
        break;
    }
}

