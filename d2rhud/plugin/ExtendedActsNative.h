#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

// Native contracts for the Act VI prototype. No installation or game mutation
// happens in this header. Its presentation records are exercised in an owned
// executable image before an installer is connected to the HUD.
namespace ExtendedActs::Native {
inline constexpr unsigned ActCount=6, ActIndex=5;
inline constexpr unsigned QuestCount=48, FirstQuest=42, QuestsPerAct=6;
inline constexpr unsigned QuestBytes=96, FirstWaypoint=39, WaypointsPerAct=9;
inline constexpr unsigned QuestTableRva=0x14b9200;
struct QuestRecord {
    uint8_t visible,button,order,act;
    uint32_t reserved;
    const uint16_t* text;
    uint32_t script,flags;
};
static_assert(sizeof(QuestRecord)==24);
struct QuestText {
    uint16_t title;
    struct State {uint16_t description,speech;} states[16];
};
static_assert(sizeof(QuestText)==66);
struct QuestLabels {uint16_t title,objective,completed;};

inline void BuildPresentation(std::span<QuestRecord,QuestCount> out,
    std::span<QuestText,QuestsPerAct> text,
    std::span<const QuestRecord,41> original,
    std::span<const QuestLabels,QuestsPerAct> labels) {
    std::memcpy(out.data(),original.data(),original.size_bytes());
    // Record 41 belongs to the original shared introduction; never reuse it.
    out[41]={0,0,0,9,0,nullptr,37,41};
    for(unsigned q=0;q<QuestsPerAct;++q) {
        text[q].title=labels[q].title;
        for(auto& state:text[q].states)state={labels[q].objective,0xe8d};
        text[q].states[11].description=labels[q].completed;
        text[q].states[13].description=labels[q].completed;
        // Keep the original scripted-quest sentinel. These six new quests
        // require their own server objectives, not cloned Act V callbacks.
        out[FirstQuest+q]={1,uint8_t(27+q),uint8_t(q),ActIndex,0,&text[q].title,37,FirstQuest+q};
    }
}
}
