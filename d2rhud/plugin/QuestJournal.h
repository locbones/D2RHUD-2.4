#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
class D2GameStrc;
class D2UnitStrc;
class D2ClientStrc;
namespace QuestJournal {
inline bool SupportsMod(std::string_view name){if(name.size()!=6)return false;constexpr std::string_view expected="rmd-mp";for(size_t n=0;n<name.size();++n){const char c=name[n]>='A'&&name[n]<='Z'?char(name[n]+32):name[n];if(c!=expected[n])return false;}return true;}
struct RecipeGate {size_t row=0;int quest=0;bool turnIn=false;int enabled=0,inputs=0,op=0,param=0,value=0;};
struct Definition {
    int id=0;std::string title,objective,turnIn;
    std::vector<std::string> objectiveRecipes,rewardRecipes;
    // Version 2 objectives are observations, not legacy proof recipes.
    std::string trigger,tips,reward;
    struct Item {std::string code,type,unique,set;int quality=0,stat=-1,min=0,max=INT32_MAX;};
    std::vector<Item> items;
    std::vector<size_t> recipes;
    int target=1,boss=-1;
    bool milestone=false,bonus=false;
};
struct Rules {
    size_t rows=0;uint64_t digest=0,previousMenuDigest=0,previousPotionDigest=0,recipeQuestMask=0;bool events=false;std::vector<RecipeGate> gates;std::vector<Definition> quests;
    std::string error;
};
inline bool RecipeObjective(const Rules& rules,const RecipeGate& gate){return rules.events&&!gate.turnIn&&gate.quest>0&&size_t(gate.quest)<=rules.quests.size()&&rules.quests[gate.quest-1].trigger=="questRecipe";}
inline bool ConfirmationQuest(int id){return id==10||id==17||id==24||id==28||id==31||id==33||id==34;}
struct Progress {bool available=false;int completed=0;bool objectiveReady=false;};
struct View {
    bool connected=false,blocked=false,objectiveReady=false,legacyAvailable=false,dirty=false;
    int completed=0,legacyCompleted=0;uint32_t recipient=0,session=0,sequence=0;
    uint32_t kills=0;uint64_t evidence=0;
    std::string character,message;
};
// Existing startup asset reader supplies the mod files; drawing does no file I/O.
void Configure(const std::filesystem::path& install,const std::string& mod,const std::string& cubeSource,const std::string& config);
std::shared_ptr<const Rules> Bindings();
void SetAvailable(bool available);
std::string Status();
Progress Inspect(D2GameStrc*,D2UnitStrc*);
void Refused(D2GameStrc*,D2UnitStrc*,const std::string&);
// A quest transaction starts before native input consumption. A successful
// native output transaction advances once; failed preflights advance nothing.
bool Begin(D2GameStrc*,D2UnitStrc*,const RecipeGate&);
enum class Outcome { Rejected,Committed,Ambiguous };
void Finish(D2GameStrc*,D2UnitStrc*,const RecipeGate&,Outcome);
using ProofRecovery=bool(*)(D2GameStrc*,D2UnitStrc*,int);
void SetProofRecovery(ProofRecovery);
void BeforeLoad(D2GameStrc*,D2ClientStrc*);
void AfterLoad(D2GameStrc*,D2ClientStrc*,D2UnitStrc*,bool success);
void Saved(D2GameStrc*,D2UnitStrc*,const uint8_t*,size_t);
void Removed(D2GameStrc*,D2UnitStrc*);
void Destroyed(D2GameStrc*);
void Tick(D2GameStrc*);
void Killed(D2GameStrc*,D2UnitStrc*,uint32_t victimId,uint32_t spawnSeed,int monsterClass,int superUniqueId);
void Crafted(D2GameStrc*,D2UnitStrc*,size_t recipe,const std::vector<uint32_t>& inputCodes,const std::vector<D2UnitStrc*>& outputs,Outcome outcome=Outcome::Committed);
void DepositedRune(D2GameStrc*,D2UnitStrc*,uint32_t code);
using RewardClaim=bool(*)(D2GameStrc*,D2UnitStrc*,int);
void SetRewardClaim(RewardClaim);
void Viewed(const char* panel);
void PollMenus();
void RequestAcknowledge(const View&);
void RequestReward(const View&);
bool Command(D2GameStrc*,D2UnitStrc*,const uint8_t*,size_t);
bool Receive(const uint8_t*,size_t);
View CurrentView();
void RequestLegacyImport(const View&);
void RequestFreshStart(const View&);
void RequestProofRecovery(const View&);
void RequestRefresh();
}
