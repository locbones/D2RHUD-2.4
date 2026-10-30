#pragma once
#include "QuestJournal.h"
#include "SummonPersistenceData.h"
#include <algorithm>
#include <charconv>
#include <sstream>
#include <optional>
#include <set>

namespace QuestJournal::Data {
using Identity=SummonPersistence::Data::Identity;
inline constexpr int MaxQuests=128;
inline constexpr size_t MaxCheckpoints=32,MaxBytes=256*1024;
inline uint64_t Hash(std::string_view text,uint64_t value=14695981039346656037ULL){for(unsigned char c:text){value^=c;value*=1099511628211ULL;}return value;}
inline std::string Hex(uint64_t value){std::ostringstream s;s<<std::hex<<value;return s.str();}
inline int Number(const std::string& value,int fallback=0){int n=0;auto r=std::from_chars(value.data(),value.data()+value.size(),n);return r.ec==std::errc{}&&r.ptr==value.data()+value.size()?n:fallback;}
inline std::vector<std::string> Fields(const std::string& line){std::vector<std::string> out;size_t start=0;for(size_t i=0;i<=line.size();++i)if(i==line.size()||line[i]=='\t'){auto v=line.substr(start,i-start);if(!v.empty()&&v.back()=='\r')v.pop_back();out.push_back(std::move(v));start=i+1;}return out;}
inline std::pair<int,bool> QuestName(const std::string& name){
    if(name.compare(0,6,"Quest "))return {};
    const auto suffix=name.empty()?' ':name.back();if(suffix!='a'&&suffix!='b')return {};
    const int id=Number(name.substr(6,name.size()-7));return id>=1&&id<=MaxQuests?std::pair{id,suffix=='b'}:std::pair<int,bool>{};
}
// These v2 lessons predate stable activity bindings. Their exported row
// numbers move when unrelated recipes are inserted, but their activities do
// not. Resolve them against the selected cubemain source (also retained by
// the BIN package's source annotations), without changing saved lesson identity.
inline bool ActivityRecipe(const Definition& quest,const std::map<std::string,std::string>& row){
    auto get=[&](const char* key){auto it=row.find(key);return it==row.end()?std::string{}:it->second;};
    if(Number(get("enabled"))!=1)return false;
    const auto name=get("description");
    auto series=[&](std::string_view base){
        if(name==base)return true;
        const auto prefix=std::string(base)+" x";
        return name.starts_with(prefix)&&Number(name.substr(prefix.size()))>0;
    };
    if(quest.id==19&&quest.trigger=="craft")return name.starts_with("Safety Ring");
    if(quest.id==32&&quest.trigger=="craft")return name=="Upconvert Magic Jewels";
    if(get("input 1")!="Z01"||get("output")!="useitem")return false;
    if(quest.id==1&&quest.trigger=="depositSkull")return series("Add Gem");
    if(quest.id==7&&quest.trigger=="countessThul")return series("Add Rune - Thul");
    if(quest.id==2&&quest.trigger=="recipe")return name=="Remove Gem - Skull"&&get("input 2")=="Z08";
    if(quest.id==8&&quest.trigger=="recipe")return name=="Remove Rune - Amn"&&get("input 2")=="Z19";
    return false;
}
inline bool NamedActivity(const Definition& q){return (q.id==1&&q.trigger=="depositSkull")||(q.id==2&&q.trigger=="recipe")||(q.id==7&&q.trigger=="countessThul")||(q.id==8&&q.trigger=="recipe")||((q.id==19||q.id==32)&&q.trigger=="craft");}
inline std::shared_ptr<Rules> ParseRules(const std::string& text,const std::string& config){
    auto rules=std::make_shared<Rules>();std::istringstream input(text);std::string line;
    if(!std::getline(input,line)){rules->error="Quest recipe descriptions are unavailable. Provide cubemain.txt or the cubemain source in data/D2RHUD/table-metadata.json.";return rules;}
    auto header=Fields(line);if(!header.empty()&&header[0].starts_with("\xef\xbb\xbf"))header[0].erase(0,3);std::map<std::string,size_t> columns;
    for(size_t i=0;i<header.size();++i)columns[header[i]]=i;
    for(const char* name:{"description","enabled","numinputs","op","param","value","input 1","output"})if(!columns.contains(name)){rules->error="cubemain.txt is missing required quest columns.";return rules;}
    std::vector<std::string> sourceRows;std::map<int,Definition> definitions;uint64_t digest=Hash("D2RHUD/character-quests/v1");
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const size_t row=rules->rows++;sourceRows.push_back(line);auto values=Fields(line);
        auto get=[&](const std::string& key){auto it=columns.find(key);return it!=columns.end()&&it->second<values.size()?values[it->second]:std::string{};};
        const auto [id,claim]=QuestName(get("description"));if(!id)continue;
        RecipeGate gate{row,id,claim,Number(get("enabled")),Number(get("numinputs")),Number(get("op")),Number(get("param")),Number(get("value"))};
        if(!gate.enabled)continue;
        if(gate.inputs<1||gate.inputs>48||(claim&&(gate.op!=18||gate.param!=398||gate.value!=id))){rules->error="Quest recipe progression gates are unsupported; quests were left inactive.";return rules;}
        rules->gates.push_back(gate);digest=Hash(get("description")+"\n"+line+"\n",digest);
        auto& def=definitions[id];def.id=id;def.title="Quest "+std::to_string(id);
        std::string recipe;
        for(int n=1;n<=7;++n){auto item=get("input "+std::to_string(n));if(!item.empty()){if(!recipe.empty())recipe+=" + ";recipe+=item;}}
        recipe+=" -> ";bool first=true;
        for(const char* key:{"output","output b","output c"}){auto item=get(key);if(!item.empty()){if(!first)recipe+=" + ";recipe+=item;first=false;}}
        (claim?def.rewardRecipes:def.objectiveRecipes).push_back(recipe);
    }
    if(definitions.empty()){rules->error="This mod has no supported Quest <number>a/b recipe chain.";return rules;}
    for(int id=1;id<=int(definitions.size());++id){
        auto it=definitions.find(id);if(it==definitions.end()||it->second.objectiveRecipes.empty()||it->second.rewardRecipes.empty()){rules->error="Quest definitions must form a complete consecutive objective/turn-in chain.";return rules;}
        it->second.objective="Complete the objective cube recipe, then turn in its Parchment of Proof.";
        it->second.turnIn="Cube the appropriate Evil Barrier charm with this quest's Parchment of Proof. Milestone quests also require their listed reward-choice ingredients.";
        rules->quests.push_back(std::move(it->second));
    }
    rules->digest=digest;
    if(!config.empty())try{
        const auto j=nlohmann::json::parse(config);
        rules->events=j.at("version")==2;
        if((j.at("version")!=1&&!rules->events)||!j.at("quests").is_array())throw std::runtime_error("Unsupported quest description file.");
        std::set<int> used;uint64_t previousMenuDigest=digest,previousPotionDigest=digest;
        for(const auto& entry:j.at("quests")){
            const int id=entry.at("id").get<int>();if(id<1||id>int(rules->quests.size())||!used.insert(id).second)throw std::runtime_error("Invalid or duplicate quest ID.");
            auto& q=rules->quests[id-1];
            for(const auto& [key,target]:std::vector<std::pair<const char*,std::string*>>{{"title",&q.title},{"objective",&q.objective},{"turnIn",&q.turnIn}})if(entry.contains(key)){
                auto value=entry.at(key).get<std::string>();if(value.empty()||value.size()>4096||value.find('\0')!=value.npos)throw std::runtime_error("Invalid quest text.");*target=std::move(value);
            }
            for(const auto& [key,target]:std::vector<std::pair<const char*,std::vector<std::string>*>>{{"objectiveRecipes",&q.objectiveRecipes},{"rewardRecipes",&q.rewardRecipes}})if(entry.contains(key)){
                const auto& list=entry.at(key);if(!list.is_array()||(!rules->events&&list.empty())||list.size()>64)throw std::runtime_error("Invalid recipe descriptions.");target->clear();
                for(const auto& value:list){auto description=value.get<std::string>();if(description.empty()||description.size()>4096||description.find('\0')!=description.npos)throw std::runtime_error("Invalid recipe description.");target->push_back(std::move(description));}
            }
            if(rules->events){
                q.trigger=entry.at("trigger").get<std::string>();q.tips=entry.value("tips",std::string{});q.reward=entry.value("reward",std::string{});
                const std::set<std::string> kinds{"item","craft","recipe","questRecipe","depositSkull","countessThul","dropWeapon","statueCube","kills","acknowledge"};
                if(!kinds.contains(q.trigger)||id>64)throw std::runtime_error("Unsupported event objective.");
                q.target=entry.value("target",1);q.boss=entry.value("boss",-1);q.milestone=entry.value("milestone",false);q.bonus=entry.value("bonus",false);
                if(q.target<1||q.target>10000000||q.boss< -1||q.boss>65535)throw std::runtime_error("Invalid event target.");
                for(const auto& value:entry.at("items")){
                    Definition::Item item;item.code=value.value("code",std::string{});item.type=value.value("type",std::string{});item.unique=value.value("unique",std::string{});item.set=value.value("set",std::string{});
                    item.quality=value.value("quality",0);item.stat=value.value("stat",-1);item.min=value.value("min",0);item.max=value.value("max",INT32_MAX);
                    if(int(!item.code.empty())+int(!item.type.empty())+int(!item.unique.empty())+int(!item.set.empty())!=1||item.stat< -1||item.stat>=512||item.min>item.max||item.quality<0||item.quality>9)throw std::runtime_error("Invalid item objective.");
                    q.items.push_back(std::move(item));
                }
                if(std::any_of(q.items.begin(),q.items.end(),[](const auto& item){return item.code.size()>4||item.type.size()>4||item.unique.size()>128||item.set.size()>128;}))throw std::runtime_error("Invalid item name.");
                if(q.items.size()>64)throw std::runtime_error("Too many item alternatives.");
                q.recipes=entry.at("recipes").get<std::vector<size_t>>();for(auto row:q.recipes)if(row>=rules->rows)throw std::runtime_error("Invalid activity recipe binding.");
                if(((q.trigger=="item"||q.trigger=="craft"||q.trigger=="statueCube")&&q.items.empty())||((q.trigger=="recipe"||q.trigger=="depositSkull"||q.trigger=="countessThul")&&q.recipes.empty())||(q.trigger=="countessThul"&&q.boss<0))throw std::runtime_error("Incomplete activity definition.");
                if(q.trigger=="questRecipe"){
                    if(!ConfirmationQuest(id)||q.milestone||!q.recipes.empty()||!q.items.empty())throw std::runtime_error("Unsupported journal recipe objective.");
                    const std::map<int,std::pair<std::string,std::string>> ingredients{{10,{"wms","yps"}},{17,{"isc","wms"}},{24,{"tsc","yps"}},{28,{"tsc","wms"}},{31,{"tsc,qty=2",""}},{33,{"isc,qty=2",""}},{34,{"isc","tsc"}}};
                    const auto& expected=ingredients.at(id);
                    bool found=false;
                    for(const auto& gate:rules->gates)if(gate.quest==id&&!gate.turnIn){
                        const auto values=Fields(sourceRows[gate.row]);auto cell=[&](const char* key){const auto c=columns.find(key);return c!=columns.end()&&c->second<values.size()?values[c->second]:std::string{};};
                        const auto a=cell("input 1"),b=cell("input 2");
                        const bool matches=(a==expected.first&&b==expected.second)||(!expected.second.empty()&&a==expected.second&&b==expected.first);
                        if(gate.inputs!=2||gate.op||!matches||cell("output")!="Bt"+std::to_string(id)||!cell("output b").empty()||!cell("output c").empty())throw std::runtime_error("Quest "+std::to_string(id)+" confirmation recipe differs from its expected ingredients.");
                        for(int n=3;n<=7;++n)if(!cell(("input "+std::to_string(n)).c_str()).empty())throw std::runtime_error("Unexpected confirmation recipe input.");
                        found=true;
                    }if(!found)throw std::runtime_error("Quest "+std::to_string(id)+" confirmation recipe is missing.");rules->recipeQuestMask|=uint64_t(1)<<(id-1);
                }
                for(auto row:q.recipes){digest=Hash(sourceRows[row],digest);previousMenuDigest=Hash(sourceRows[row],previousMenuDigest);previousPotionDigest=Hash(sourceRows[row],previousPotionDigest);}
                if(q.recipes.size()>512)throw std::runtime_error("Too many recipe bindings.");
                auto identity=entry;for(const char* field:{"title","objective","turnIn","tips","reward","objectiveRecipes","rewardRecipes"})identity.erase(field);
                digest=Hash(identity.dump(),digest);
                auto menuIdentity=identity;
                if(ConfirmationQuest(id)&&q.trigger=="questRecipe")menuIdentity["trigger"]="acknowledge";
                previousMenuDigest=Hash(menuIdentity.dump(),previousMenuDigest);
                if(id!=10)identity=menuIdentity;
                previousPotionDigest=Hash(identity.dump(),previousPotionDigest);
            }
        }
        if(rules->events&&used.size()!=rules->quests.size())throw std::runtime_error("Every quest requires an event definition.");
        if(rules->recipeQuestMask&&previousMenuDigest!=digest)rules->previousMenuDigest=previousMenuDigest;
        if(rules->recipeQuestMask&&previousPotionDigest!=digest)rules->previousPotionDigest=previousPotionDigest;
    }catch(const std::exception& e){rules->error=std::string("Quest description configuration is invalid: ")+e.what();}
    // Hash the original configuration above so correcting legacy row drift
    // neither resets progress nor permits previously claimed rewards again.
    if(rules->events&&rules->error.empty()){
        for(auto& q:rules->quests)if(NamedActivity(q))q.recipes.clear();
        for(size_t n=0;n<sourceRows.size();++n){
            const auto values=Fields(sourceRows[n]);std::map<std::string,std::string> row;
            for(const char* key:{"description","enabled","input 1","input 2","output"}){
                const auto index=columns.find(key);if(index!=columns.end()&&index->second<values.size())row.emplace(key,values[index->second]);
            }
            for(auto& q:rules->quests)if(NamedActivity(q)&&ActivityRecipe(q,row))q.recipes.push_back(n);
        }
        for(const auto& q:rules->quests)if(NamedActivity(q)&&q.recipes.empty()){
            rules->error="Quest "+std::to_string(q.id)+" activity recipe is missing from the selected cube table.";break;
        }
    }
    rules->digest=digest;return rules;
}
struct State {int completed=0;bool objectiveReady=false,legacyClosed=false,uncertain=false;uint32_t kills=0;uint64_t evidence=0;bool countess=false,thul=false;uint16_t menus=0;bool operator==(const State&)const=default;};
inline bool Valid(const State& s,int count){return s.completed>=0&&s.completed<=count&&(!s.objectiveReady||s.completed<count);}
inline bool Allows(const State& s,const RecipeGate& gate,int count){return !s.uncertain&&Valid(s,count)&&gate.quest==s.completed+1&&(gate.turnIn?s.objectiveReady:!s.objectiveReady);}
inline bool Advance(State& s,const RecipeGate& gate,int count){if(!Allows(s,gate,count))return false;if(gate.turnIn){++s.completed;s.objectiveReady=false;}else s.objectiveReady=true;s.legacyClosed=true;return true;}
inline bool Import(State& s,int completed,int count){if(s.uncertain||s.legacyClosed||s.completed||s.objectiveReady||completed<1||completed>count)return false;s.completed=completed;s.objectiveReady=false;s.legacyClosed=true;return true;}
// Checksum fields exist only to read old journals. They never select progress.
struct Checkpoint {uint32_t checksum=0,saveSize=0;State state;};
struct History {Identity identity;uint64_t digest=0,revision=0;std::vector<Checkpoint> checkpoints;};
inline Identity Stable(Identity identity){
    identity.created=0;identity.flags&=0x24;
    for(auto& c:identity.name)if(c>='A'&&c<='Z')c=char(c+32);
    return identity;
}
inline bool SameCharacter(const Identity& a,const Identity& b){return SummonPersistence::Data::SameIdentity(Stable(a),Stable(b));}
inline std::string Key(const Identity& identity){return SummonPersistence::Data::Key(Stable(identity));}
inline bool Header(const uint8_t* bytes,size_t size,Identity& identity,Checkpoint& point){
    auto get=[&]<class T>(size_t at){T value{};if(at+sizeof(T)<=size)memcpy(&value,bytes+at,sizeof(T));return value;};
    if(!bytes||size<0x14f||get.operator()<uint32_t>(0)!=0xaa55aa55||get.operator()<uint32_t>(4)!=98)return false;
    if(bytes[0x10b+60])return false;
    identity.name.assign(reinterpret_cast<const char*>(bytes+0x10b),strnlen_s(reinterpret_cast<const char*>(bytes+0x10b),61));
    identity.created=0;identity.classId=bytes[0x28];identity.flags=get.operator()<uint32_t>(0x24)&0x24;
    point={};return SummonPersistence::Data::ValidIdentity(identity);
}
inline State Merge(State a,const State& b){
    const auto completed=(std::max)(a.completed,b.completed);
    const bool ready=(a.completed==completed&&a.objectiveReady)||(b.completed==completed&&b.objectiveReady);
    const bool uncertain=(a.completed==completed&&a.uncertain)||(b.completed==completed&&b.uncertain);
    a.completed=completed;a.objectiveReady=ready;a.uncertain=uncertain;
    a.legacyClosed=a.legacyClosed||b.legacyClosed||completed||ready;
    a.kills=(std::max)(a.kills,b.kills);a.evidence|=b.evidence;a.menus|=b.menus;a.countess|=b.countess;a.thul|=b.thul;return a;
}
inline State Current(const History& history){State state;for(const auto& point:history.checkpoints)state=Merge(state,point.state);return state;}
inline void Append(History& history,Checkpoint point){point.checksum=point.saveSize=0;history.checkpoints.assign(1,point);}
inline std::optional<State> Find(const History& history,uint32_t,uint32_t){return history.checkpoints.empty()?std::optional<State>{}:Current(history);}
inline bool Pristine(const History& history){return Current(history)==State{};}
inline nlohmann::json EncodeState(const State& state){
    return {{"completed",state.completed},{"ready",state.objectiveReady},{"legacyClosed",state.legacyClosed},{"uncertain",state.uncertain},
        {"kills",state.kills},{"evidence",state.evidence},{"countess",state.countess},{"thul",state.thul},{"menus",state.menus}};
}
inline std::optional<State> DecodeState(const nlohmann::json& value){try{
    using namespace SummonPersistence::Data;State state;uint32_t completed=0,kills=0,menus=0;
    if(!UnsignedInteger(value,"completed",MaxQuests,completed)||!UnsignedInteger(value,"kills",UINT32_MAX,kills)||
       !UnsignedInteger(value,"menus",127,menus)||!value.at("evidence").is_number_unsigned())return {};
    state.completed=int(completed);state.kills=kills;state.menus=uint16_t(menus);state.evidence=value.at("evidence").get<uint64_t>();
    state.objectiveReady=value.at("ready").get<bool>();state.legacyClosed=value.at("legacyClosed").get<bool>();state.uncertain=value.at("uncertain").get<bool>();
    state.countess=value.value("countess",false);state.thul=value.value("thul",false);
    if(!Valid(state,MaxQuests))return {};if(state.completed||state.objectiveReady)state.legacyClosed=true;return state;
}catch(...){return {};}}
inline std::string Encode(const History& history){
    if(!SummonPersistence::Data::ValidIdentity(history.identity)||history.checkpoints.empty()||!Valid(Current(history),MaxQuests))throw std::runtime_error("Invalid quest progress.");
    const auto identity=Stable(history.identity);
    return nlohmann::json{{"version",2},{"revision",history.revision},{"identity",{{"name",history.identity.name},{"classId",identity.classId},{"flags",identity.flags}}},{"state",EncodeState(Current(history))}}.dump(2);
}
inline std::optional<History> Decode(std::string_view text,int){try{
    if(text.empty()||text.size()>MaxBytes)return {};
    const auto document=nlohmann::json::parse(text);const int version=document.at("version");if(version!=1&&version!=2)return {};
    History history;const auto& identity=document.at("identity");history.identity.name=identity.at("name").get<std::string>();
    using namespace SummonPersistence::Data;
    if(!ValidIdentity(history.identity)||!UnsignedInteger(identity,"classId",UINT32_MAX,history.identity.classId)||!UnsignedInteger(identity,"flags",UINT32_MAX,history.identity.flags))return {};
    if(version==2){
        if(!document.at("revision").is_number_unsigned())return {};history.revision=document.at("revision").get<uint64_t>();
        auto state=DecodeState(document.at("state"));if(!state)return {};Append(history,{0,0,*state});
    }else{
        // Obsolete timestamps, hashes and checkpoint save metadata are ignored.
        const auto& points=document.at("checkpoints");if(!points.is_array()||points.empty()||points.size()>4096)return {};
        for(auto point:points){for(const char* field:{"kills","evidence","menus"})if(!point.contains(field))point[field]=uint64_t(0);
            auto state=DecodeState(point);if(!state)continue;history.checkpoints.push_back({0,0,*state});}
        if(history.checkpoints.empty())return {};
    }return history;
}catch(...){return {};}}
inline bool AtomicWrite(const std::filesystem::path& path,const History& h,std::string& error){
    HANDLE file=INVALID_HANDLE_VALUE;std::filesystem::path temp;bool owns=false;
    try{
        auto text=Encode(h);if(text.size()>MaxBytes)throw std::runtime_error("Quest history exceeds its size limit.");
        std::filesystem::create_directories(path.parent_path());static std::atomic<uint64_t> serial{0};
        for(unsigned n=0;n<32;++n){temp=path;temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file!=INVALID_HANDLE_VALUE)break;if(GetLastError()!=ERROR_ALREADY_EXISTS&&GetLastError()!=ERROR_FILE_EXISTS)throw std::system_error(int(GetLastError()),std::system_category());}
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Unable to reserve a quest save file.");owns=true;
        DWORD written=0;if(!WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr)||written!=text.size()||!FlushFileBuffers(file))throw std::system_error(int(GetLastError()),std::system_category());
        CloseHandle(file);file=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::system_error(int(GetLastError()),std::system_category());
        return true;
    }catch(const std::exception& e){error=e.what();if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(owns)DeleteFileW(temp.c_str());return false;}
}
inline constexpr uint64_t Magic=0xDEADBEEFDEADBEEF;
inline constexpr uint8_t Operation=7,Version=3;
#pragma pack(push,1)
struct Packet {
    uint8_t opcode=0x2b;uint64_t magic=Magic;uint8_t operation=Operation,version=Version;
    uint32_t recipient=0,session=0,sequence=0;uint64_t definition=0;
    uint8_t completed=0,count=0,ready=0,flags=0,legacyCompleted=0;uint32_t kills=0;uint64_t evidence=0;
    uint64_t revision=0;uint16_t menus=0;uint8_t classId=0,characterFlags=0,events=0;
    char character[61]{},message[84]{};
};
struct CommandPacket {uint8_t opcode=3;uint64_t magic=Magic;uint8_t operation=Operation,version=Version;uint32_t recipient=0,session=0,sequence=0;uint8_t action=0,proposal=0,detail=0;std::array<uint8_t,184> padding{};};
struct Transfer {uint64_t revision=0;uint8_t completed=0,ready=0,legacyClosed=0,uncertain=0;uint32_t kills=0;uint64_t evidence=0;uint16_t menus=0;uint8_t countess=0,thul=0;};
#pragma pack(pop)
static_assert(sizeof(Packet)==0xce&&sizeof(CommandPacket)==0xd2&&sizeof(Transfer)<=184);
inline Transfer Pack(const State& s,uint64_t revision){return {revision,uint8_t(s.completed),uint8_t(s.objectiveReady),uint8_t(s.legacyClosed),uint8_t(s.uncertain),s.kills,s.evidence,s.menus,uint8_t(s.countess),uint8_t(s.thul)};}
inline std::optional<State> Unpack(const Transfer& p){
    State s{p.completed,p.ready!=0,p.legacyClosed!=0,p.uncertain!=0,p.kills,p.evidence,p.countess!=0,p.thul!=0,p.menus};
    if(p.ready>1||p.legacyClosed>1||p.uncertain>1||p.countess>1||p.thul>1||p.menus>127||!Valid(s,MaxQuests))return {};return s;
}
inline bool Valid(const Packet& p){return p.opcode==0x2b&&p.magic==Magic&&p.operation==Operation&&p.version==Version&&p.count&&p.count<=MaxQuests&&p.completed<=p.count&&p.ready<=1&&(!p.ready||p.completed<p.count)&&!(p.flags&~31)&&p.legacyCompleted<=p.count&&p.menus<=127&&!(p.events&~7)&&!(p.characterFlags&~0x24)&&p.character[0]&&!p.character[60]&&!p.message[83];}
}
