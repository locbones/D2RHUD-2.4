#pragma once
#include "LootFilterRuleBuilder.h"
#include <cmath>
#include <climits>
#include <cstdlib>
#include <memory>
#include <unordered_map>

namespace LootFilterConfig::Preview {
using Builder::Catalog;
struct Context {
    int difficulty = 2, area = 2, filterLevel = 1, playerLevel = 85, location = 3;
    std::string itemCode;
};
struct Sample {
    std::string code, name;
    int quality = 2, tier = 0, level = 85, sockets = 0, maximum = 0, index = 0;
    bool ethereal = false, identified = true, runeword = false, collected = false, focused = true;
    std::vector<int> types;
    std::map<std::pair<int,int>,int> stats;
};
struct Result {
    std::string name, description, reason, messages;
    bool matched = false, hidden = false, audio = false;
    std::array<float,4> background{0,0,0,0.9f}, border{0,0,0,0};
    float thickness = 1;
};
inline constexpr const char* Qualities[] = {"Unknown","Inferior","Normal","Superior","Magic","Set","Rare","Unique","Crafted","Tempered"};

// Accept data expressions only; previewing never runs authored functions,
// variable initializers, file access or generated config code.
inline bool DataExpression(const std::string& raw,int depth=0) {
    if(depth>20||raw.size()>256*1024)return false;
    const auto text=Builder::Trim(raw);Document doc;
    if(doc.IsString(text)||text=="true"||text=="false"||text=="nil")return true;
    if(!text.empty()){
        char* end=nullptr;double value=std::strtod(text.c_str(),&end);
        if(end==text.c_str()+text.size()&&end!=text.c_str()&&std::isfinite(value))return true;
    }
    for(const char* operation:{"NOT","OR"}){
        const std::string name=operation;
        if(text.compare(0,name.size(),name)==0){auto value=Builder::Trim(text.substr(name.size()));if(!value.empty()&&value.front()=='('&&value.back()==')')value=value.substr(1,value.size()-2);return DataExpression(value,depth+1);}
    }
    if(text.empty()||text.front()!='{'||text.back()!='}'||!doc.Parse("return {value="+text+"}"))return false;
    try {const auto* field=doc.Find("value");for(const auto& child:doc.Table(doc.TokenAt(field->valueBegin)))if(!DataExpression(doc.Value(child),depth+1))return false;return true;}
    catch(...){return false;}
}
inline std::vector<int> Types(const Catalog& catalog,const std::vector<std::string>& roots) {
    std::set<std::string> seen;std::vector<std::string> pending=roots;std::vector<int> result;
    while(!pending.empty()){
        auto code=pending.back();pending.pop_back();if(!seen.insert(code).second)continue;
        auto id=catalog.typeIds.find(code);if(id!=catalog.typeIds.end())result.push_back(id->second);
        auto parents=catalog.typeParents.find(code);if(parents!=catalog.typeParents.end())pending.insert(pending.end(),parents->second.begin(),parents->second.end());
    }
    return result;
}
inline int ExampleNumber(const Builder::Value& value,bool passing) {
    long long number=value.number;
    switch(value.operation){
    case 0:if(!passing)++number;break;case 1:if(!passing)--number;break;
    case 2:if(!passing)++number;break;case 3:if(passing)++number;break;
    case 4:if(passing)--number;break;case 5:if(passing)++number;break;
    case 6:number=(std::min)(value.number,value.upper);if(!passing)--number;break;
    }
    return int(std::clamp(number,static_cast<long long>(INT_MIN),static_cast<long long>(INT_MAX)));
}
inline std::vector<Sample> Samples(const std::string& rule,const Catalog& catalog,const std::string& itemCode={}) {
    std::vector<std::string> focus;
    auto fields=Builder::RuleFields(rule);
    for(const auto& field:fields)if(Builder::Canonical(field.first)=="codes"){
        auto value=Builder::Decode(*Builder::FindSpec("codes"),field.second);
        if(value.editable&&!value.exclude)for(const auto& code:value.choices)if(code!="allitems"&&focus.size()<2)focus.push_back(code);
    }
    if(focus.empty())for(const auto& field:fields)if(Builder::Canonical(field.first)=="itype"){
        auto value=Builder::Decode(*Builder::FindSpec("itype"),field.second);
        if(!value.editable||value.exclude)continue;
        for(const auto& base:catalog.bases){auto types=Types(catalog,base.second.types);bool found=false;
            for(const auto& choice:value.choices){int id=0;if(!Builder::Integer(choice,id))for(const auto& option:catalog.types)if(option.label==choice)Builder::Integer(option.value,id);if(std::find(types.begin(),types.end(),id)!=types.end())found=true;}
            if(found){focus.push_back(base.first);break;}
        }
    }
    if(!itemCode.empty())focus={itemCode};
    if(focus.empty())focus={"rin"};
    std::vector<Sample> result;
    auto make=[&](const std::string& code,int quality,int variant,bool runeword=false){
        Sample sample;sample.code=code;sample.quality=quality;sample.name=code;
        auto base=catalog.bases.find(code);
        if(base!=catalog.bases.end()){sample.name=base->second.name;sample.tier=base->second.tier;sample.maximum=base->second.sockets;sample.types=Types(catalog,base->second.types);}
        else{
            for(const auto& item:catalog.items)if(item.value==code)sample.name=item.label;
            if(code=="rin"){sample.name="Ring";sample.types={10};}if(code=="amu"){sample.name="Amulet";sample.types={12};}
        }
        sample.sockets=sample.maximum?(std::min)(sample.maximum,variant%2?4:0):0;
        sample.ethereal=sample.maximum>0&&variant%3==1;sample.identified=variant%3!=2;sample.collected=(quality==5||quality==7)&&variant%2==1;
        sample.level=variant%2?85:35;
        sample.stats={{{39,0},variant%2?40:10},{{41,0},20},{{43,0},20},{{45,0},20}};
        for(const auto& field:fields){const auto key=Builder::Canonical(field.first);const auto* spec=Builder::FindSpec(key);if(!spec)continue;
            auto value=Builder::Decode(*spec,field.second);if(!value.editable)continue;
            if(key=="stat")sample.stats[{value.stat,value.parameter}]=ExampleNumber(value,variant%2!=0);
            if(key=="ilvl")sample.level=std::clamp(ExampleNumber(value,variant%2!=0),1,999);
            if(key=="sockets")sample.sockets=std::clamp(ExampleNumber(value,variant%2!=0),0,sample.maximum);
            if(key=="identified")sample.identified=variant%2?value.flag:!value.flag;
            if(key=="ethereal"&&sample.maximum>0)sample.ethereal=variant%2?value.flag:!value.flag;
            if(key=="grail"&&(quality==5||quality==7))sample.collected=variant%2?value.flag:!value.flag;
            if(key=="runeword"&&(quality==2||quality==3)&&sample.maximum>0)sample.runeword=variant%2?value.flag:!value.flag;
            if(key=="index"&&value.operation==0)sample.index=value.number;
        }
        if(quality==5||quality==7)for(const auto& named:catalog.namedItems)if(named.code==code&&named.quality==(quality==5?"Set":"Unique")&&(sample.index==0||sample.index==named.index)){sample.name=named.label;sample.index=named.index;break;}
        if(runeword){sample.runeword=true;sample.quality=2;sample.identified=true;sample.sockets=sample.maximum?sample.maximum:4;}
        result.push_back(std::move(sample));
    };
    for(const auto& code:focus)for(int quality=1;quality<=9;++quality){make(code,quality,quality*2+1);make(code,quality,quality*2);}
    for(const auto& pair:std::vector<std::pair<std::string,int>>{{"crs",2},{"crs",3},{"rin",4},{"amu",6},{"r24",2},{"uap",7}}){make(pair.first,pair.second,int(result.size()));result.back().focused=false;}
    make("crs",2,10,true);result.back().focused=false;
    return result;
}

class Session {
    struct Memory { size_t used=0; } memory;
    lua_State* lua=nullptr;
    std::string config;
    std::string setupMessages;
    Result* receiving=nullptr;
    int instructions=0;
    static void* Allocate(void* user,void* block,size_t oldSize,size_t size) {
        auto& state=*static_cast<Memory*>(user);if(!block)oldSize=0;
        if(!size){state.used-=oldSize;std::free(block);return nullptr;}
        if(size>32*1024*1024||state.used-oldSize>32*1024*1024-size)return nullptr;
        void* next=std::realloc(block,size);if(next)state.used=state.used-oldSize+size;return next;
    }
    static Session& Self(lua_State* state){return **static_cast<Session**>(lua_getextraspace(state));}
    static void Budget(lua_State* state,lua_Debug*){if((Self(state).instructions-=1000)<=0)luaL_error(state,"Preview instruction limit exceeded");}
    static int NoFile(lua_State* state){lua_pushnil(state);return 1;}
    static int Time(lua_State* state){lua_pushinteger(state,0);return 1;}
    static int Print(lua_State* state){
        auto& self=Self(state);
        size_t size=0;const char* data=lua_tolstring(state,1,&size);if(!data)return 0;
        std::string text(data,(std::min)(size,size_t(2048)));
        if(!self.receiving){if(self.setupMessages.size()<4096){if(!self.setupMessages.empty())self.setupMessages+='\n';self.setupMessages+=text;}return 0;}
        if(text.compare(0,12,"[LootFilter ")==0){
            auto end=text.find("] ");auto marker=end==std::string::npos?std::string::npos:text.find(" matches",end);
            if(marker!=std::string::npos)self.receiving->matched=true;
            auto reason=text.find(" failures: ");if(reason!=std::string::npos)self.receiving->reason=text.substr(reason+11);
        }else if(self.receiving->messages.size()<4096){if(!self.receiving->messages.empty())self.receiving->messages+='\n';self.receiving->messages+=text;}
        return 0;
    }
    static int LoadConfig(lua_State* state){
        const char* filename=lua_tostring(state,1);
        if(!filename||std::string(filename)!="lootfilter_config.lua"){lua_pushnil(state);lua_pushliteral(state,"Preview has no filesystem access");return 2;}
        auto& self=Self(state);
        if(luaL_loadbufferx(state,self.config.data(),self.config.size(),"preview config","t")){lua_pushnil(state);lua_insert(state,-2);return 2;}
        if(lua_istable(state,3)){lua_pushvalue(state,3);lua_setupvalue(state,-2,1);}
        return 1;
    }
    bool Run(const std::string& source,const char* label,int returns=0){
        instructions=2000000;lua_sethook(lua,Budget,LUA_MASKCOUNT,1000);
        int code=luaL_loadbufferx(lua,source.data(),source.size(),label,"t");if(!code)code=lua_pcall(lua,0,returns,0);
        if(code){const char* message=lua_tostring(lua,-1);error=message?message:"Preview failed";if(!setupMessages.empty())error+='\n'+setupMessages;lua_pop(lua,1);return false;}return true;
    }
    static std::string String(lua_State* state,int table,const char* key){lua_getfield(state,table,key);size_t size=0;const char* text=lua_tolstring(state,-1,&size);std::string result=text?std::string(text,(std::min)(size,size_t(8192))):"";lua_pop(state,1);return result;}
    void ReadResult(size_t index){
        lua_getglobal(lua,"_previewResults");lua_rawgeti(lua,-1,lua_Integer(index+1));int table=lua_gettop(lua);auto& value=results[index];
        value.name=String(lua,table,"Name");value.description=String(lua,table,"Description");
        lua_getfield(lua,table,"Hide");value.hidden=lua_toboolean(lua,-1)!=0;lua_pop(lua,1);
        for(auto pair:{std::make_pair("Background",&value.background),std::make_pair("Border",&value.border)}){
            lua_getfield(lua,table,pair.first);if(lua_istable(lua,-1))for(int channel=0;channel<4;++channel){lua_rawgeti(lua,-1,channel+1);if(lua_isnumber(lua,-1))(*pair.second)[channel]=float(std::clamp(lua_tonumber(lua,-1),0.,1.));lua_pop(lua,1);}
            if(std::string(pair.first)=="Border"&&lua_istable(lua,-1)){lua_rawgeti(lua,-1,5);if(lua_isnumber(lua,-1))value.thickness=float(std::clamp(lua_tonumber(lua,-1),0.,12.));lua_pop(lua,1);}lua_pop(lua,1);
        }
        lua_pop(lua,2);
    }
public:
    std::string error;
    std::vector<Sample> samples;
    std::vector<Result> results;
    Session()=default;
    Session(const Session&)=delete;
    ~Session(){if(lua)lua_close(lua);}
    bool Build(const std::string& engine,const std::string& rule,const std::unordered_map<std::string,std::string>& globals,const Catalog& catalog,const Context& context){
        if(lua){lua_close(lua);lua=nullptr;}error.clear();setupMessages.clear();samples.clear();results.clear();
        if(engine.empty()){error="The installed filter engine is unavailable.";return false;}
        Document document;if(!document.Parse("return {rules={"+rule+"\n}}")||document.rules.size()!=1){error="This custom rule cannot be previewed.";return false;}
        try{document.Table(document.TokenAt(document.rules[0].valueBegin));}catch(...){error="This custom rule cannot be previewed.";return false;}
        const auto fields=Builder::RuleFields(rule);
        bool style=false,audio=false;
        for(const auto& field:fields){if(!DataExpression(field.second)){error="Custom expression in '"+field.first+"' cannot be previewed. The rule is preserved.";return false;}if(field.first=="style"&&Builder::Trim(field.second)!="nil")style=true;if(field.first=="audio"&&Builder::Trim(field.second)!="nil")audio=true;}
        std::string styles;
        if(style){auto found=globals.find("styles");if(found==globals.end()||!DataExpression(found->second)){error="The custom style cannot be previewed.";return false;}styles="styles="+found->second+",";}
        auto language=globals.find("language");
        config="return {strict=true,debug={mode='all',limit=10000},allowOverrides=false,audioPlayback=false,language="+Quote(language==globals.end()?"enUS":language->second)+","+styles+"rules={"+rule+"\n}}";
        lua=lua_newstate(Allocate,&memory);if(!lua){error="Unable to allocate the preview.";return false;}*static_cast<Session**>(lua_getextraspace(lua))=this;
        for(auto library:{std::make_pair(LUA_GNAME,luaopen_base),std::make_pair(LUA_STRLIBNAME,luaopen_string),std::make_pair(LUA_TABLIBNAME,luaopen_table),std::make_pair(LUA_MATHLIBNAME,luaopen_math),std::make_pair(LUA_UTF8LIBNAME,luaopen_utf8)}){luaL_requiref(lua,library.first,library.second,1);lua_pop(lua,1);}
        for(const char* name:{"dofile","load","collectgarbage"}){lua_pushnil(lua);lua_setglobal(lua,name);}
        lua_newtable(lua);lua_pushcfunction(lua,NoFile);lua_setfield(lua,-2,"open");lua_setglobal(lua,"io");
        lua_newtable(lua);lua_pushcfunction(lua,Time);lua_setfield(lua,-2,"time");lua_pushcfunction(lua,Time);lua_setfield(lua,-2,"clock");lua_setglobal(lua,"os");
        lua_newtable(lua);lua_newtable(lua);lua_setfield(lua,-2,"loaded");lua_setglobal(lua,"package");
        lua_pushcfunction(lua,LoadConfig);lua_setglobal(lua,"loadfile");lua_pushcfunction(lua,Print);lua_setglobal(lua,"print");
        if(!Run(engine,"installed filter engine"))return false;
        if(!Run("local d=GetFilterDiagnostics(); assert(d.disabled==0,'Rule is invalid'); _previewDisabled=(d.rules==0)","preview validation"))return false;
        samples=Samples(rule,catalog,context.itemCode);results.resize(samples.size());
        std::string data="_previewSamples={";
        for(size_t i=0;i<samples.size();++i){const auto& sample=samples[i];data+="{ID="+std::to_string(i+1)+",name="+Quote(sample.name)+",code="+Quote(sample.code)+",quality="+std::to_string(sample.quality)+",tier="+std::to_string(sample.tier)+",level="+std::to_string(sample.level)+",index="+std::to_string(sample.index)+",sockets="+std::to_string(sample.sockets)+",maximum="+std::to_string(sample.maximum)+",ethereal="+(sample.ethereal?"true":"false")+",identified="+(sample.identified?"true":"false")+",runeword="+(sample.runeword?"true":"false")+",collected="+(sample.collected?"true":"false")+",stats={";
            for(const auto& stat:sample.stats)data+="["+Quote(std::to_string(stat.first.first)+":"+std::to_string(stat.first.second))+"]="+std::to_string(stat.second)+",";
            data+="},types={";for(int type:sample.types)data+="["+std::to_string(type)+"]=true,";data+="}},";
        }
        data+="}; _previewContext={difficulty="+std::to_string(context.difficulty)+",area="+std::to_string(context.area)+",level="+std::to_string(context.filterLevel)+",playerLevel="+std::to_string(context.playerLevel)+",location="+std::to_string(context.location)+"}";
        if(!Run(data,"preview samples"))return false;
        if(!Run(R"(
            _previewItems={};_previewResults={}
            function _previewApply(i)
                local s=_previewSamples[i];local c=_previewContext
                local item={ID=s.ID,Name=s.name,Txt={Code=s.code},Data={Quality=s.quality,FileIndex=s.index,ItemLevel=s.level,Flags=(s.identified and 0x10 or 0)+(s.runeword and 0x4000000 or 0),IsEthereal=s.ethereal,Owner=c.location==-1 and 500 or 1},Mode=c.location==-1 and 0 or c.location,Area=c.area,Rarity=s.tier}
                item.Stat=function(_,id,param) if id==194 then return s.sockets end;return s.stats[tostring(id)..':'..tostring(param or 0)] or 0 end
                item.MaxSockets=function()return s.maximum end;item.IsType=function(_,id)return s.types[id] or false end
                item.Link=function()return s.name end
                local me={ID=1,Difficulty=c.difficulty,Stat=function(_,id) if id==12 then return c.playerLevel elseif id==0 or id==2 then return 150 elseif id==1 or id==3 then return 80 end;return 0 end}
                local result={Name=s.name,Description='Example item tooltip',Background={0,0,0,0.9},Border={0,0,0,0,1},Grail={isGrail=s.quality==5 or s.quality==7,collected=s.collected}}
                ApplyFilter(me,item,result,c.level);result._baseName=result.Name
                _previewItems[i]=item;_previewResults[i]=result
            end
            function _previewAnimate(tick)
                for i,result in ipairs(_previewResults) do
                    if result.BackgroundFunction then result.BackgroundFunction(_previewItems[i],result,tick) end
                    if result.NameFunction then result.Name=result.NameFunction(_previewItems[i],result._baseName,tick) end
                end
            end
        )","preview adapter"))return false;
        for(size_t i=0;i<samples.size();++i){receiving=&results[i];bool ok=Run("_previewApply("+std::to_string(i+1)+")","sample evaluation");receiving=nullptr;if(!ok)return false;results[i].audio=audio&&results[i].matched;ReadResult(i);if(!results[i].matched&&results[i].reason.empty())results[i].reason="Item, filter level or enabled state does not match";}
        return Animate(0);
    }
    bool Animate(unsigned tick){
        if(!lua||!error.empty())return false;
        if(!Run("_previewAnimate("+std::to_string(tick)+")","sample effects"))return false;
        for(size_t i=0;i<results.size();++i)ReadResult(i);return true;
    }
};
}
