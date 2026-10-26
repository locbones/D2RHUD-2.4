// Use the real queue, update, rendering and config code with a headless ImGui
// context. Native combat callbacks stay installed after disable in production;
// QueueGameDamage must therefore enforce the toggle itself.
#include "../d2rhud/plugin/FloatingDamage.cpp"
#include <cassert>
#include <iostream>
#include <thread>
#include "settings-tabs-test.h"

namespace D3D12 {
ImFont* GetFloatingDamageFont(int) { return ImGui::GetIO().Fonts->Fonts[0]; }
}

using namespace FloatingDamage;

void Fresh(bool enabled) {
    SetEnabled(false);
    g_numbers.clear();
    { std::lock_guard lock(g_queueMutex); g_pendingEvents.clear(); }
    ResetToDefaults();
    SetEnabled(enabled);
}

int RenderVertices() {
    ImGui::NewFrame();
    auto* list=ImGui::GetBackgroundDrawList();
    const auto before=list->VtxBuffer.Size;
    FloatingDamage::Render(list,ImGui::GetIO().DisplaySize);
    const auto count=list->VtxBuffer.Size-before;
    ImGui::EndFrame();
    return count;
}

void Configuration() {
    Fresh(true);
    LoadFromJson(nlohmann::json{{"FloatingDamage",false},{"FloatingDamageSettings",{{"Enabled",true},{"TextSize",55.0}}}});
    assert(!IsEnabled() && !GetConfig().enabled && GetConfig().textSize==55.0f);
    LoadFromJson(nlohmann::json{{"FloatingDamage",true},{"FloatingDamageSettings",{{"Enabled",false},{"TextSize",61.0}}}});
    assert(IsEnabled() && GetConfig().enabled && GetConfig().textSize==61.0f);
    LoadFromJson(nlohmann::json{{"FloatingDamageSettings",{{"Enabled",false}}}});
    assert(!IsEnabled());
    LoadFromJson(nlohmann::json{{"DamageNumbers",{{"Enabled",true},{"FontSize",44.0}}}});
    assert(IsEnabled() && GetConfig().textSize==44.0f);
    LoadFromJson(nlohmann::json{{"FloatingDamage",false},{"DamageNumbers",{{"Enabled",true}}}});
    assert(!IsEnabled());
    for(bool enabled:{true,false,true,false}) {
        SetEnabled(enabled);
        nlohmann::ordered_json saved;
        SaveToJson(saved);
        assert(saved["FloatingDamage"]==enabled && saved["FloatingDamageSettings"]["Enabled"]==enabled);
        SetEnabled(!enabled);
        LoadFromJson(nlohmann::json::parse(saved.dump()));
        assert(IsEnabled()==enabled && GetConfig().enabled==enabled);
        ResetToDefaults();
        assert(IsEnabled()==enabled && GetConfig().enabled==enabled);
    }
}

void LiveDamage() {
    Fresh(true);
    QueueGameDamage(100,500,500,1,42,Kind::Normal,Element::Physical);
    QueueGameDamage(40,500,500,1,42,Kind::Normal,Element::Poison);
    Update(0.016f);
    assert(ActiveCount()>0 && g_rollingDamage==140 && RenderVertices()>0);
    QueueGameDamage(70,500,500,1,42,Kind::Critical);
    assert(PendingCount()==1);
    SetEnabled(false);
    assert(!HasDisplayActivity() && g_dpsSamples.empty() && g_rollingDamage==0 && RenderVertices()==0);
    QueueGameDamage(300,500,500,1,42,Kind::Normal);
    QueueDamage(400,500,500,1,42,Kind::Critical);
    Update(0.016f);
    assert(!HasDisplayActivity() && RenderVertices()==0);
    SetEnabled(true);
    Update(0.016f);
    assert(ActiveCount()==0 && g_rollingDamage==0); // No queued hits reappear.
    QueueGameDamage(25,500,500,1,42,Kind::Normal);
    Update(0.016f);
    assert(ActiveCount()==1 && g_rollingDamage==25 && RenderVertices()>0);
    Update(1.0f);
    assert(ActiveCount()==0 && g_rollingDamage==25);
    Update(5.0f);
    assert(g_dpsSamples.empty() && g_rollingDamage==0); // DPS continues aging without numbers.
}

void Preview() {
    Fresh(false);
    QueuePreviewAt(500,500,Kind::Normal);
    QueueGameDamage(50,500,500,1,42,Kind::Normal);
    assert(PendingCount()==1);
    Update(0.016f);
    assert(ActiveCount()==1 && g_numbers[0].preview && g_rollingDamage==0 && RenderVertices()>0);
    SetEnabled(true);
    QueueGameDamage(75,500,500,1,42,Kind::Normal);
    Update(0.016f);
    assert(ActiveCount()==2 && g_rollingDamage==75);
    QueuePreviewAt(600,500,Kind::Critical);
    QueueGameDamage(90,500,500,1,42,Kind::Critical);
    SetEnabled(false);
    assert(ActiveCount()==1 && g_numbers[0].preview && PendingCount()==1 && g_rollingDamage==0);
    Update(0.016f);
    assert(ActiveCount()==2 && std::all_of(g_numbers.begin(),g_numbers.end(),[](const auto& number){return number.preview;}));
    assert(g_dpsSamples.empty() && RenderVertices()>0);
    Update(2.0f);
    assert(!HasDisplayActivity() && RenderVertices()==0);
}

void RepeatedTogglesWithProducer() {
    Fresh(false);
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        while(!stop.load()) {
            QueueGameDamage(1,500,500,1,42,Kind::Normal);
            std::this_thread::yield();
        }
    });
    for(int i=0;i<100;++i) {
        SetEnabled(true);
        QueueGameDamage(5,500,500,1,42,Kind::Normal);
        Update(0.016f);
        assert(ActiveCount()>0);
        SetEnabled(false);
        assert(PendingCount()==0 && ActiveCount()==0 && g_rollingDamage==0);
        QueueGameDamage(10,500,500,1,42,Kind::Normal);
        assert(PendingCount()==0);
    }
    stop=true;
    producer.join();
    assert(!HasDisplayActivity());
    SetEnabled(true);
    Update(0.016f);
    assert(ActiveCount()==0 && g_rollingDamage==0);
}

int main() {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize={1920,1080};
    io.DeltaTime=1.0f/60.0f;
    io.Fonts->AddFontDefault();
    assert(io.Fonts->Build());
    Configuration();
    LiveDamage();
    Preview();
    RepeatedTogglesWithProducer();
    ExerciseSettingsTabs("Damage settings tabs","DamageSettingsSections",6,[]{DrawSettingsPanel(1);});
    ImGui::DestroyContext();
    std::cout<<"Floating Damage: config precedence/round-trips, live on/off rendering, pending/active cleanup, preview isolation, DPS reset and 100 concurrent toggles passed.\n";
}
