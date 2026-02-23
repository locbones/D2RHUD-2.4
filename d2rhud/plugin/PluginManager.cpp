#include "PluginManager.h"
#include "D2RHUD/D2RHUD.h"
#include <imgui.h>

PluginManager::PluginManager() {
    m_Plugins.push_back(new D2RHUD());
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
bool PluginManager::TryCloseMenuOnEscape() {
    for (auto& plugin : m_Plugins) {
        if (plugin->TryCloseMenuOnEscape())
            return true;
    }
    return false;
}

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

        // --- Add mouse button events ---
    case WM_LBUTTONDOWN:
        for (auto& plugin : m_Plugins) {
            if (plugin->OnKeyPressed(VK_LBUTTON)) break;
        }
        break;

    case WM_RBUTTONDOWN:
        for (auto& plugin : m_Plugins) {
            if (plugin->OnKeyPressed(VK_RBUTTON)) break;
        }
        break;

    case WM_MBUTTONDOWN:
        for (auto& plugin : m_Plugins) {
            if (plugin->OnKeyPressed(VK_MBUTTON)) break;
        }
        break;

    case WM_XBUTTONDOWN:
    {
        short xBtn = HIWORD(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        for (auto& plugin : m_Plugins) {
            if (plugin->OnKeyPressed(xBtn)) break;
        }
        break;
    }

    default:
        break;
    }
}


