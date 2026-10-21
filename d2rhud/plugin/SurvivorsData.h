#pragma once
#include "CubeMapData.h"
#include <random>

namespace CubeMap::Survivors {
constexpr uint8_t Operation=3,Version=5;
constexpr uint32_t FramesPerSecond=25,Duration=600,FinalBossAt=540;
constexpr size_t MaxLiving=128;
enum class Upgrade:uint8_t {Arcane,Frost,Storm,Power,Haste,Reach,Fleet,Mending,Guard,Count};
constexpr size_t Count=size_t(Upgrade::Count);
struct UpgradeInfo {const char* name;const char* description;uint8_t maximum;};
inline constexpr std::array<UpgradeInfo,Count> Upgrades{{
    {"Primary Skill","Strengthens your class's first automatic skill.",5},
    {"Secondary Skill","Unlocks or strengthens your class's second automatic skill.",5},
    {"Third Skill","Unlocks or strengthens your class's third automatic skill.",5},
    {"Empower","Adds two effective levels to all unlocked class skills per rank.",8},
    {"Quickening","Your automatic attacks recharge faster.",5},
    {"Far Reach","Increases automatic target acquisition range.",5},
    {"Fleet Footed","Adds 8% movement speed per rank during the run.",5},
    {"Mending","Slowly restores life during the run.",5},
    {"Iron Ward","Adds 10% physical resistance per rank, up to 30%.",3},
}};
constexpr uint8_t NoUpgrade=255;
struct ClassInfo {const char* name;std::array<const char*,3> skills;std::array<int,3> ids;};
inline constexpr std::array<ClassInfo,7> Classes{{
    {"Amazon",{"Lightning Fury","Plague Javelin","Charged Strike"},{35,25,24}},
    {"Sorceress",{"Fire Ball","Ice Bolt","Charged Bolt"},{47,39,38}},
    {"Necromancer",{"Teeth","Bone Spear","Poison Nova"},{67,84,92}},
    {"Paladin",{"Blessed Hammer","Holy Bolt","Fist of the Heavens"},{112,101,121}},
    {"Barbarian",{"Bash","Battle Cry","Berserk"},{126,146,152}},
    {"Druid",{"Fissure","Volcano","Tornado"},{234,244,245}},
    {"Assassin",{"Fire Blast","Shock Web","Wake of Fire"},{251,256,262}},
}};
#pragma pack(push,1)
struct Profile {
    uint32_t xp=0;uint8_t level=1;
    std::array<uint8_t,Count> ranks{1};
    std::array<uint16_t,4> attributes{}; // Strength, Energy, Dexterity, Vitality.
    bool operator==(const Profile&)const=default;
};
#pragma pack(pop)
inline bool Valid(const Profile& p){
    if(!p.level || p.level>30 || p.xp>1000000 || !p.ranks[0])return false;
    unsigned total=0;for(size_t i=0;i<Count;++i){if(p.ranks[i]>Upgrades[i].maximum)return false;total+=p.ranks[i];}
    unsigned spent=0;for(auto value:p.attributes)spent+=value;
    return total==p.level && spent<=unsigned(p.level-1)*5;
}
inline const char* UpgradeName(uint8_t classId,size_t n){return n<3 && classId<Classes.size()?Classes[classId].skills[n]:Upgrades[n].name;}
inline uint32_t RequiredXP(unsigned level){return 12+level*8+level*level*2;}
struct Build {
    uint32_t xp=0,kills=0,offer=0,offeredAt=0;
    uint8_t level=1,classId=0;
    std::array<uint8_t,Count> ranks{1};
    std::array<uint16_t,4> attributes{};
    std::array<uint8_t,3> choices{NoUpgrade,NoUpgrade,NoUpgrade};
    std::array<uint32_t,3> nextAttack{};
    bool allocating=false;
    Profile Save()const{return {xp,level,ranks,attributes};}
    void Load(const Profile& p,uint8_t selected){xp=p.xp;level=p.level;ranks=p.ranks;attributes=p.attributes;classId=selected;}
    unsigned Unspent()const{unsigned spent=0;for(auto n:attributes)spent+=n;return unsigned(level-1)*5-spent;}
    bool Pending()const{return choices[0]!=NoUpgrade;}
    bool Paused()const{return Pending() || allocating;}
    void Offer(std::mt19937& random,uint32_t frame){
        if(Paused() || level>=30 || xp<RequiredXP(level))return;
        std::vector<uint8_t> pool;
        for(size_t n=0;n<Count;++n)if(ranks[n]<Upgrades[n].maximum)pool.push_back(uint8_t(n));
        if(pool.empty())return;
        std::shuffle(pool.begin(),pool.end(),random);choices.fill(NoUpgrade);
        std::copy_n(pool.begin(),(std::min)(pool.size(),choices.size()),choices.begin());
        if(!++offer)++offer;offeredAt=frame;
    }
    bool Choose(uint32_t nonce,uint8_t slot){
        if(!Pending() || nonce!=offer || slot>=choices.size() || choices[slot]>=Count ||
           ranks[choices[slot]]>=Upgrades[choices[slot]].maximum || xp<RequiredXP(level))return false;
        xp-=RequiredXP(level);++ranks[choices[slot]];++level;choices.fill(NoUpgrade);return true;
    }
};
inline uint32_t Cooldown(const Build& b,size_t weapon){
    constexpr uint32_t base[]{22,48,34};
    return (std::max)(6u,base[weapon]*(100-10*b.ranks[size_t(Upgrade::Haste)])/100);
}
inline int Range(const Build& b,size_t weapon){
    int base=22;
    if(b.classId==4 && weapon!=1)base=3;
    else if(b.classId==0 && weapon==2)base=3;
    else if(b.classId==2 && weapon==2)base=10;
    else if(b.classId==3 && weapon==0)base=10;
    return base+2*b.ranks[size_t(Upgrade::Reach)];
}
inline unsigned WaveSize(uint32_t seconds,unsigned players){return (std::min)(24u,(4+seconds/40)*std::clamp(players,1u,4u));}
enum class Action:uint8_t {Start,Choose,Leave,Ready,Allocate};
enum class Failure:uint8_t {None,Busy,Npc,State,Native,Loading,Avatar,Profile,Interaction,Disabled};
inline const char* Message(Failure f){switch(f){
case Failure::Busy:return "A map or event is already running in this game.";
case Failure::Npc:return "Approach Charsi in the Rogue Encampment to enter.";
case Failure::State:return "This mod needs the sendable hud_cube_map state. Restart after adding it.";
case Failure::Native:return "Survivors is unavailable on this game executable.";
case Failure::Loading:return "The arena could not be prepared. Please try again.";
case Failure::Avatar:return "The separate Survivors character could not be created. Your hero is unchanged.";
case Failure::Profile:return "Survivors progress could not be loaded or saved. Check the roster file.";
case Failure::Interaction:return "Close the NPC interaction and put the cursor item away before entering Survivors.";
case Failure::Disabled:return "Survivor Arena is disabled in the host's D2RHUD Options > Workshop.";
default:return "";}}
#pragma pack(push,1)
struct SnapshotBody {
    uint8_t opcode=0x2b;uint64_t magic=Data::PacketMagic;uint8_t operation=Operation,version=Version;
    uint32_t run=0,session=0,recipient=0,sequence=0;
    Data::EventPhase phase=Data::EventPhase::None;Failure failure=Failure::None;
    uint32_t xp=0,requiredXP=0,kills=0,offer=0;uint16_t wave=0;uint8_t level=1,choiceSeconds=0,classId=0;
    std::array<uint8_t,Count> ranks{};
    std::array<uint8_t,3> choices{NoUpgrade,NoUpgrade,NoUpgrade};
    std::array<uint16_t,4> attributes{};
    uint8_t paused=0;
    uint8_t allocating=0;
    std::array<int32_t,4> totalAttributes{};
};
struct Snapshot {SnapshotBody data;std::array<uint8_t,sizeof(Data::Packet)-sizeof(SnapshotBody)> padding{};};
struct RequestBody {
    uint8_t opcode=3;uint64_t magic=Data::PacketMagic;uint8_t operation=Operation,version=Version;
    Action action=Action::Start;uint32_t run=0,session=0,offer=0;uint8_t slot=0;
    uint8_t classId=0;Profile profile{};
    uint16_t points=0,spent=0;
};
struct Request {RequestBody data;std::array<uint8_t,sizeof(Data::CommandPacket)-sizeof(RequestBody)> padding{};};
#pragma pack(pop)
static_assert(sizeof(Snapshot)==0xce && sizeof(Request)==0xd2);
inline bool Valid(const SnapshotBody& p){
    if(p.opcode!=0x2b || p.magic!=Data::PacketMagic || p.operation!=Operation || p.version!=Version ||
       !p.recipient || p.phase>Data::EventPhase::Cancelled || p.failure>Failure::Disabled || p.classId>=Classes.size() || !p.level || p.level>30 ||
       p.choiceSeconds>12 || p.paused>1 || p.allocating>1 || (p.allocating && (!p.paused || !p.offer || p.choices[0]!=NoUpgrade)) || p.wave>1000 || p.kills>100000 || p.requiredXP>RequiredXP(30))return false;
    unsigned spent=0;for(auto value:p.attributes)spent+=value;if(spent>unsigned(p.level-1)*5)return false;
    for(auto value:p.totalAttributes)if(value<0 || value>1000000)return false;
    std::set<uint8_t> choices;
    for(size_t n=0;n<Count;++n)if(p.ranks[n]>Upgrades[n].maximum)return false;
    for(auto choice:p.choices)if(choice!=NoUpgrade && (choice>=Count || !choices.insert(choice).second || !p.offer || p.ranks[choice]>=Upgrades[choice].maximum))return false;
    return true;
}
inline bool Valid(const RequestBody& p){return p.opcode==3 && p.magic==Data::PacketMagic && p.operation==Operation && p.version==Version && p.action<=Action::Allocate && p.classId<Classes.size() && (p.action!=Action::Choose || p.slot<3) && (p.action!=Action::Allocate || (p.slot<4 && p.points && p.points<=145 && p.spent<=145)) && (p.action!=Action::Start || Valid(p.profile));}
}
