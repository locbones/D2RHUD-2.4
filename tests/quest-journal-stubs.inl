#include "../d2rhud/plugin/QuestJournal.h"
std::shared_ptr<const QuestJournal::Rules> testQuestRules;
QuestJournal::Progress testQuestProgress;
bool testQuestBegin=true;
std::vector<QuestJournal::Outcome> testQuestOutcomes;
struct TestQuestCraft {size_t row;std::vector<uint32_t> inputs;size_t outputs;};
std::vector<TestQuestCraft> testQuestCrafts;
namespace QuestJournal {
std::shared_ptr<const Rules> Bindings(){return testQuestRules;}
Progress Inspect(D2GameStrc*,D2UnitStrc*){return testQuestProgress;}
void Refused(D2GameStrc*,D2UnitStrc*,const std::string&){}
bool Begin(D2GameStrc*,D2UnitStrc*,const RecipeGate&){return testQuestBegin;}
void Finish(D2GameStrc*,D2UnitStrc*,const RecipeGate&,Outcome outcome){testQuestOutcomes.push_back(outcome);}
void Configure(const std::filesystem::path&,const std::string&,const std::string&,const std::string&){}
void SetProofRecovery(ProofRecovery){}
void SetRewardClaim(RewardClaim){}
void Crafted(D2GameStrc*,D2UnitStrc*,size_t row,const std::vector<uint32_t>& inputs,const std::vector<D2UnitStrc*>& outputs){testQuestCrafts.push_back({row,inputs,outputs.size()});}
void DepositedRune(D2GameStrc*,D2UnitStrc*,uint32_t){}
void BeforeLoad(D2GameStrc*,D2ClientStrc*){}
void AfterLoad(D2GameStrc*,D2ClientStrc*,D2UnitStrc*,bool){}
void Saved(D2GameStrc*,D2UnitStrc*,const uint8_t*,size_t){}
void Removed(D2GameStrc*,D2UnitStrc*){}
void Destroyed(D2GameStrc*){}
void Tick(D2GameStrc*){}
}
