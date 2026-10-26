#pragma once
#include "CubeVisualData.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CubeMap::Data {
// Full destination IDs belong to our output record and run, never to the
// one-byte native portal InteractType. A marker needs both fields to match.
constexpr uint16_t Marker = 0x4d50;
constexpr uint8_t PacketOperation = 1;
constexpr uint64_t PacketMagic = 0xDEADBEEFDEADBEEF;
constexpr size_t MaxEffects = 12;
using Output = CubeVisual::Data::Output;
constexpr uint16_t PreserveInput=0x4000;
// Only the textual zzz token encodes this marker; numeric map=0 stays invalid.
constexpr uint16_t RandomDestination=0;
struct Option { bool present=false, valid=true; int level=0; bool useItem=false; };
inline Option Parse(std::string_view input) {
    using namespace CubeVisual::Data;
    auto text=Trim(input);
    if(text.size()>1 && text.front()=='"' && text.back()=='"')text=text.substr(1,text.size()-2);
    Option result; size_t begin=0; bool unknown=false;
    while(begin<=text.size()) {
        const auto end=text.find(',',begin);
        const auto token=Trim(std::string_view(text).substr(begin,end==text.npos ? text.size()-begin : end-begin));
        const auto split=token.find('=');
        if(Equal(Trim(std::string_view(token).substr(0,split)),"map")) {
            if(result.present || split==token.npos)result.valid=false;
            result.present=true;
            const auto value=split==token.npos ? std::string{} : Trim(std::string_view(token).substr(split+1));
            if(Equal(value,"zzz"))result.level=RandomDestination;
            else {
                int level=0; const auto parsed=std::from_chars(value.data(),value.data()+value.size(),level);
                if(value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || level<=0 || level>65535)result.valid=false;
                result.level=level;
            }
        }else if(Equal(token,"useitem")) {if(result.useItem)result.valid=false;result.useItem=true;}
        else if(!token.empty())unknown=true;
        if(end==text.npos)break;
        begin=end+1;
    }
    if(result.present && unknown)result.valid=false;
    return result;
}
inline bool IsMap(const Output& output) {return output.type==4 && output.special==Marker;}
inline Output Encode(int level) {Output result;result.type=4;result.special=Marker;result.base=uint16_t(level);return result;}
inline std::atomic<int> additionalTown{0};
inline bool Town(int level) {return level==1 || level==40 || level==75 || level==103 || level==109 || (level>0 && level==additionalTown.load());}
enum class Side : uint8_t { Monster, Player };
struct Effect {
    Side side; const char* stat; const char* label; int minimum,maximum; bool percent;
    const char* pairedStat=nullptr;const char* pairedLabel=nullptr;
    int pairedMultiplier=1,pairedDivisor=1;
};
// Ranges and effect types follow the original ReMoDDeD map affixes. These are
// real game stats, rather than the old map_e_*/map_p_* display-only properties.
inline constexpr std::array Effects{
    Effect{Side::Monster,"item_maxhp_percent","Monster life",10,50,true},
    Effect{Side::Monster,"item_armor_percent","Monster defense",10,100,true},
    Effect{Side::Monster,"fireresist","Monster fire resistance",10,40,true},
    Effect{Side::Monster,"lightresist","Monster lightning resistance",10,45,true},
    Effect{Side::Monster,"coldresist","Monster cold resistance",10,40,true},
    Effect{Side::Monster,"poisonresist","Monster poison resistance",10,40,true},
    Effect{Side::Monster,"damageresist","Monster physical resistance",5,20,true},
    Effect{Side::Monster,"magicresist","Monster magic resistance",5,20,true},
    Effect{Side::Monster,"item_fastermovevelocity","Monster movement speed",5,25,true},
    Effect{Side::Monster,"item_fasterattackrate","Monster attack speed",5,25,true},
    Effect{Side::Monster,"item_crushingblow","Monster crushing blow",1,13,true},
    Effect{Side::Monster,"item_allskills","Monster skill levels",1,3,false},
    Effect{Side::Player,"fireresist","Player fire resistance",-40,-10,true},
    Effect{Side::Player,"lightresist","Player lightning resistance",-40,-10,true},
    Effect{Side::Player,"coldresist","Player cold resistance",-40,-10,true},
    Effect{Side::Player,"poisonresist","Player poison resistance",-40,-10,true},
    Effect{Side::Player,"item_armor_percent","Player defense",-25,-5,true},
    Effect{Side::Player,"item_maxhp_percent","Player maximum life",-12,-5,true},
    Effect{Side::Player,"item_fastercastrate","Player cast speed",-20,-5,true},
    Effect{Side::Player,"item_fasterattackrate","Player attack speed",-20,-5,true},
    Effect{Side::Player,"item_fastermovevelocity","Player movement speed",-10,-5,true},
    Effect{Side::Player,"item_magicbonus","Player magic find",25,75,true},
    Effect{Side::Player,"item_goldbonus","Player extra gold",25,75,true},
    Effect{Side::Player,"item_addexperience","Player experience",1,10,true},
    Effect{Side::Player,"item_healafterkill","Player life after kill",1,5,false},
    Effect{Side::Player,"item_manaafterkill","Player mana after kill",1,5,false},
    Effect{Side::Monster,"level","Additional monster levels",2,6,false},
    Effect{Side::Monster,nullptr,"Treasure Surge (extra treasure-class picks)",1,3,false},
    Effect{Side::Player,"damagepercent","Glass Cannon (physical damage)",40,80,true,"item_maxhp_percent","Player maximum life",-1,4},
    Effect{Side::Player,"item_fasterattackrate","Battle Frenzy (attack speed)",15,35,true,"item_fastercastrate","Player cast speed"},
    Effect{Side::Player,"lifedrainmindam","Blood Feast (life stolen per hit)",4,10,true,"manadrainmindam","Mana stolen per hit",1,2},
    Effect{Side::Player,"manarecoverybonus","Second Wind (mana regeneration)",30,70,true,"item_fastergethitrate","Player hit recovery",1,2},
    Effect{Side::Player,"item_fastermovevelocity","Fleet Footed (movement speed)",15,35,true},
    Effect{Side::Monster,"item_maxhp_percent","Featherweights (monster life)",-30,-15,true,"item_fastermovevelocity","Monster movement speed",-1,1},
    Effect{Side::Monster,"damagepercent","Heavy Hitters (monster physical damage)",20,45,true},
};
// Preserve existing effect IDs; companion effects and TC picks do not occupy
// additional wire entries and are derived identically by host and client.
inline constexpr size_t LevelEffect=26,TreasureEffect=27;
inline int PairedValue(const Effect& effect,int value){return value*effect.pairedMultiplier/effect.pairedDivisor;}
inline bool ConflictingEffects(size_t first,size_t second){
    const auto& a=Effects[first];const auto& b=Effects[second];if(a.side!=b.side)return false;
    for(const auto x:{a.stat,a.pairedStat})for(const auto y:{b.stat,b.pairedStat})if(x && y && std::string_view(x)==y)return true;
    return false;
}
struct Roll {uint8_t effect=0; int16_t value=0;};
inline bool Valid(Roll roll) {
    return roll.effect<Effects.size() && roll.value>=Effects[roll.effect].minimum && roll.value<=Effects[roll.effect].maximum;
}
enum class Phase : uint8_t { Preparing, Open, Running, Returning, Closed };
// Missing units remain outstanding until an observed death. A room unload,
// conversion or disappearance must never count as a kill.
struct Census {
    std::map<uint32_t,bool> dead;
    bool ready=false, entered=false;
    void Observe(uint32_t id,bool isDead) {dead[id]=isDead;}
    uint32_t Remaining() const {return uint32_t(std::count_if(dead.begin(),dead.end(),[](const auto& entry){return !entry.second;}));}
    bool Complete() const {return ready && entered && Remaining()==0;}
};
enum class Event : uint8_t { None, GoldenArmada, ForsakenFestival, FountainOfYouth, SpeedDemon, MidasMeddling, GoblinGrotto, Trial, Survivors,
    TreasureTrove, GoldenGoose, ChampionClash, PlentifulPirating, SoulSurprise };
inline bool RegularEvent(Event event){return event>=Event::GoldenArmada && event<=Event::MidasMeddling;}
inline bool TimedEvent(Event event){return event==Event::TreasureTrove || event==Event::GoldenGoose || event==Event::PlentifulPirating;}
inline int EventLevel(Event event){return TimedEvent(event)?0:event==Event::ChampionClash?108:event==Event::SoulSurprise?239:39;}
enum class EventPhase : uint8_t { None, WaitingTown, Ceremony, Briefing, Entering, Active, Returning, Finished, Cancelled };
enum class EventFailure : uint8_t { None, Initializer, Roster, PlayerStats, Occupied, Reset, Placement, Arrival, Participant, Portal, TreasureClass, ChampionTreasureClass };
inline const char* FailureMessage(EventFailure failure){
    switch(failure){
    case EventFailure::Initializer:return "The event's monster initialization is unavailable.";
    case EventFailure::Roster:return "No eligible event monsters were found in the loaded tables.";
    case EventFailure::PlayerStats:return "Required player effect stats are missing from the loaded tables.";
    case EventFailure::Occupied:return "The event area is occupied by a player, corpse, pet or observer.";
    case EventFailure::Reset:return "The event area could not be loaded or reset.";
    case EventFailure::Placement:return "Not enough safe positions were available for the encounter.";
    case EventFailure::Arrival:return "The event area's arrival position is unavailable.";
    case EventFailure::Participant:return "No event participant is ready in town.";
    case EventFailure::Portal:return "There is no safe position for the event portals.";
    case EventFailure::TreasureClass:return "MapGroupA is missing from the loaded treasure classes. Restart after adding it.";
    case EventFailure::ChampionTreasureClass:return "ChampClash is missing from the loaded treasure classes. Restart after adding it.";
    default:return "The realm is currently unavailable.";
    }
}
struct EventInfo {const char* name;const char* description;uint32_t seconds;
    const char* scoutingReport=nullptr;const char* location=nullptr;const char* duration=nullptr;};
inline constexpr std::array EventInfos{
    EventInfo{"", "", 0},
    EventInfo{"Golden Armada", "Unique item drop chance +500%.",300},
    EventInfo{"Forsaken Festival", "Set item drop chance +500%.",300},
    EventInfo{"Fountain of Youth", "Your mana never runs out.",300},
    EventInfo{"Speed Demon", "Maximum movement, attack and cast speed.",300},
    EventInfo{"Midas' Meddling", "Every monster drops gold. Gold value +500%.",300},
    EventInfo{"Goblin Grotto", "An entire Cow Level of treasure goblins. You have one minute!",60,
        "It appears goblins are congregating in a secret location. Now's our time to attack adventurer!\nI doubt we will have much time, the gods do not pamper us it seems.",
        "Moo Moo Farm","One Minute"},
    EventInfo{"Trials of Tribulation", "Defeat five level 125 bosses in five minutes. Each has at least three immunities. Victory awards +2 stat points.",300,
        "Death. None of the rogues managed to return from their scout...\nAll that awaits is death...We are Forsaken Adventurer!",
        "Moo Moo Farm","Five Minutes"},
    EventInfo{"Sanctuary Survivors", "Fight as your chosen Survivors class. Native class skills attack automatically. Earn saved class levels and upgrades, then slay the final boss before ten minutes expire.",600},
    EventInfo{"Treasure Trove", "Set and unique item drop chances +500% for ten minutes, wherever you travel.",600,
        "Someone, or something, appears to be arming our enemies at an alarming scale!\nOur scouts report a five-fold increase in set and unique items being found on monster corpses",
        "Everywhere","Ten Minutes"},
    EventInfo{"Golden Goose", "Gold amounts +500% for ten minutes, wherever you travel.",600,
        "It appears as if a dark stimulus package was sent to monsters around Sanctuary.\nOur scouts report a five-fold increase in market sales, apparently adventuring is quite lucrative currently...",
        "Everywhere","Ten Minutes"},
    EventInfo{"Champion Clash", "The Chaos Sanctuary is filled with champion and unique packs. Clear this instance; no time limit.",0,
        "A legion of high-ranked demons and their cohorts have been spotted amassing in the Chaos Sanctuary!\nI fear if the number continues to grow, we will be overwhelmed. Please lend us your strength adventurer...",
        "Chaos Sanctuary","None"},
    EventInfo{"Plentiful Pirating", "NoDrop and Blank outcomes are removed for two minutes. The root treasure class gains one pick; nested pick counts stay unchanged.",120,
        "An evil force seems to be funding our enemies conquest of evil!\nOur scouts report a marked increase in the equipment and supplies the demons carry...",
        "Everywhere","Two Minutes"},
    EventInfo{"Soul Surprise", "Teleport to level 239 and survive its monsters for one minute.",60,
        "Our scouts have discovered an enemy safebox hidden in a stone room!\nYour legends of heroism and capability are well known here, we'd be honored if you'd pillage it...",
        "Unknown","Unknown"},
};
inline const EventInfo& Info(Event event){return EventInfos[(std::min)(size_t(event),EventInfos.size()-1)];}
inline uint32_t EventRemaining(uint32_t now,uint32_t start,Event event){const auto duration=Info(event).seconds*25;const auto elapsed=now-start;return elapsed>=duration ? 0 : (duration-elapsed+24)/25;}
inline constexpr std::array RegularEvents{Event::GoblinGrotto,Event::TreasureTrove,Event::GoldenGoose,Event::ChampionClash,Event::PlentifulPirating,Event::SoulSurprise};
inline Event SelectEvent(uint8_t die,unsigned secretRoll,bool forceSecret){return forceSecret || secretRoll<5 ? Event::Trial : die>=1 && die<=6 ? RegularEvents[die-1] : Event::None;}
inline constexpr uint32_t CeremonyFadeMs=750, DiceRevealMs=3750, SecretRevealMs=2900;
inline constexpr uint32_t CeremonyFrames=(DiceRevealMs+2000+39)/40, BriefingFrames=250;
#pragma pack(push,1)
struct Packet {
    uint8_t opcode=0x2b; uint64_t magic=PacketMagic; uint8_t operation=PacketOperation;
    uint8_t version=5, active=0; uint32_t run=0, sequence=0, recipient=0;
    uint16_t level=0; uint32_t remaining=0; uint8_t effects=0;
    std::array<Roll,MaxEffects> rolls{};
    uint32_t entrancePortal=0,exitPortal=0;
    uint16_t town=0,monsterLevel=0;
    uint8_t completed=0,die=0,reward=0;
    uint32_t session=0;
    Event event=Event::None;EventPhase eventPhase=EventPhase::None;
    uint32_t eventElapsed=0,eventSeconds=0;
    uint8_t success=0;EventFailure failure=EventFailure::None;
    uint8_t elitePacks=0;
    std::array<uint8_t,0xce-63-MaxEffects*sizeof(Roll)> padding{};
};
struct CommandPacket {
    uint8_t opcode=3;uint64_t magic=PacketMagic;uint8_t operation=PacketOperation,version=5;
    uint32_t run=0,session=0;EventPhase phase=EventPhase::WaitingTown;
    std::array<uint8_t,0xd2-20> padding{};
};
#pragma pack(pop)
static_assert(sizeof(Packet)==0xce);
static_assert(sizeof(CommandPacket)==0xd2);
inline bool Valid(const Packet& packet) {
    if(packet.opcode!=0x2b || packet.magic!=PacketMagic || packet.operation!=PacketOperation || packet.version!=5 || packet.elitePacks>13 ||
       packet.active>1 || packet.effects>MaxEffects || packet.remaining>1000000 || (packet.active && (!packet.run || !packet.level || Town(packet.level))))return false;
    if(packet.completed>1 || packet.reward>2 || packet.die>6 || packet.success>1 || packet.event>Event::SoulSurprise || packet.eventPhase>EventPhase::Cancelled || packet.failure>EventFailure::ChampionTreasureClass)return false;
    if(packet.completed && (!packet.run || packet.die<1 || packet.active))return false;
    if(packet.reward && (packet.event!=Event::Trial || packet.eventPhase!=EventPhase::Finished || !packet.success || packet.reward!=2))return false;
    if(packet.event!=Event::None && (!packet.run || (!packet.die && packet.event!=Event::Survivors) || packet.eventPhase==EventPhase::None || packet.eventSeconds>Info(packet.event).seconds || (packet.event!=Event::ChampionClash && packet.eventElapsed>86400000)))return false;
    std::set<uint8_t> used;
    for(size_t i=0;i<packet.effects;++i){
        if(!Valid(packet.rolls[i]) || !used.insert(packet.rolls[i].effect).second)return false;
        for(size_t j=0;j<i;++j)if(ConflictingEffects(packet.rolls[i].effect,packet.rolls[j].effect))return false;
    }
    return true;
}
}
