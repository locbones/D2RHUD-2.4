#pragma once
#include <cstdint>
#include <cstddef>

// Optional startup-loader ABI. Keep the original 0/1/2 readiness export so
// older helpers remain compatible; this separate record adds useful errors.
namespace StartupDiagnostics {
inline constexpr uint32_t Magic=0x48554453,Version=1;
enum Feature : uint32_t {States=1,Classes=2,Acts=4};
enum class Stage : uint32_t {Starting,States,Core,Classes,Acts,QuestUI,Metadata,Ready,Rejected};
struct Report {
    uint32_t magic=Magic,version=Version,size=sizeof(Report);
    uint32_t stage=uint32_t(Stage::Starting),failedFeatures=0;
    char detail[1536]{};
};
static_assert(sizeof(Report)==1556 && offsetof(Report,detail)==20);
inline const char* StageName(uint32_t stage){
    switch(Stage(stage)){
    case Stage::States:return "extended states";
    case Stage::Core:return "core HUD features";
    case Stage::Classes:return "extended classes / Warlock";
    case Stage::Acts:return "Act VI";
    case Stage::QuestUI:return "quest journal shortcuts";
    case Stage::Metadata:return "table metadata export";
    case Stage::Ready:return "startup completion";
    default:return "HUD initialization";
    }
}
}
