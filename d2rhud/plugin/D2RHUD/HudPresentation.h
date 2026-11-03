#pragma once
#include "../../json.hpp"
#include <imgui.h>
#include <chrono>
#include <string>

namespace HudPresentation {
struct PreferenceState { bool wasOpen=false,dirty=false,failed=false; };
struct PreferenceHost {
    float scale=1;
    void(*pushFont)(int);void(*popFont)(int);
    bool(*title)(const char*,bool*);bool(*save)();bool(*button)(const char*);
};
void Preferences(const char* title,bool& open,ImVec2 initialSize,PreferenceState& state,
    void(*draw)(float),nlohmann::ordered_json(*snapshot)(),const PreferenceHost& host);
void Toast(const std::string& text,bool& visible,std::chrono::steady_clock::time_point began,
    std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now());
}
