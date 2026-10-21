#pragma once
#include <imgui.h>
#include <string>

// Keep std::string allocation in the HUD's CRT. The installed ImGui stdlib
// extension uses a different CRT; its cross-library string overloads are unsafe.
namespace LootFilterConfig {
inline int ResizeText(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto& text = *static_cast<std::string*>(data->UserData);
        text.resize(size_t(data->BufTextLen));
        data->Buf = text.data();
    }
    return 0;
}
inline bool InputText(const char* label, std::string* text) {
    return ImGui::InputText(label, text->data(), text->capacity() + 1, ImGuiInputTextFlags_CallbackResize, ResizeText, text);
}
inline bool InputTextWithHint(const char* label, const char* hint, std::string* text) {
    return ImGui::InputTextWithHint(label, hint, text->data(), text->capacity() + 1, ImGuiInputTextFlags_CallbackResize, ResizeText, text);
}
inline bool InputTextMultiline(const char* label, std::string* text, const ImVec2& size, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputTextMultiline(label, text->data(), text->capacity() + 1, size, flags | ImGuiInputTextFlags_CallbackResize, ResizeText, text);
}
}
