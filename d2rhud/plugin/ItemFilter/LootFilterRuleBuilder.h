#pragma once
#include "LuaConfigIO.h"
#include <array>
#include <set>
#include <limits>

namespace LootFilterConfig::Builder {
struct Option { std::string label, value; };
struct NamedItem { std::string label, code, quality; int index = 0; };
struct BaseItem { std::string name; std::vector<std::string> types; int level = 85, sockets = 0, tier = 0; };
struct Catalog {
    std::vector<Option> items, types, stats, areas;
    bool loaded = false;
    std::string status;
    std::vector<NamedItem> namedItems;
    std::map<std::string,BaseItem> bases;
    std::map<std::string,int> typeIds;
    std::map<std::string,std::vector<std::string>> typeParents;
};
enum class Kind { Choices, Number, Boolean, Text, Color, Stat };
struct Spec { const char* key; const char* label; Kind kind; bool action; };
inline constexpr Spec Specs[] = {
    {"codes","Specific items",Kind::Choices,false}, {"quality","Item quality",Kind::Choices,false},
    {"itype","Item category",Kind::Choices,false}, {"rarity","Base tier",Kind::Choices,false},
    {"ilvl","Item level",Kind::Number,false}, {"sockets","Sockets",Kind::Number,false},
    {"maxsock","Maximum sockets",Kind::Number,false}, {"identified","Identified",Kind::Boolean,false},
    {"ethereal","Ethereal",Kind::Boolean,false}, {"runeword","Runeword",Kind::Boolean,false},
    {"grail","Already collected for grail",Kind::Boolean,false}, {"location","Item location",Kind::Choices,false},
    {"difficulty","Difficulty",Kind::Choices,false}, {"area","Area",Kind::Choices,false},
    {"filter_levels","Filter level",Kind::Number,false}, {"index","Unique / set index",Kind::Number,false},
    {"stat","Item stat",Kind::Stat,false}, {"pstat","Player stat",Kind::Stat,false},
    {"hide","Hide item",Kind::Boolean,true}, {"name_override","Replace item name",Kind::Text,true},
    {"prefix","Text before item name",Kind::Text,true}, {"suffix","Text after item name",Kind::Text,true},
    {"prefix_desc","Text at tooltip bottom",Kind::Text,true}, {"suffix_desc","Text at tooltip top",Kind::Text,true},
    {"background","Label background color",Kind::Color,true}, {"border","Label border",Kind::Color,true},
    {"name_style","Name effect",Kind::Choices,true}, {"background_style","Background effect",Kind::Choices,true},
    {"notify","Chat notification",Kind::Text,true}, {"audio","Speak text / play sound",Kind::Text,true},
    {"print","Message when hidden",Kind::Text,true},
    {"hide_mismatches","Hide other matches",Kind::Boolean,true},
};
inline const Spec* FindSpec(const std::string& key) { for (const auto& spec:Specs) if(key==spec.key)return &spec;return nullptr; }
inline std::string Canonical(std::string key) {
    static const std::map<std::string,std::string> aliases = {
        {"code","codes"},{"qualities","quality"},{"rarities","rarity"},{"indices","index"},{"indexes","index"},
        {"item_level","ilvl"},{"item_levels","ilvl"},{"ilvls","ilvl"},{"socket","sockets"},
        {"maxsockets","maxsock"},{"max_sockets","maxsock"},{"itypes","itype"},{"item_type","itype"},
        {"item_types","itype"},{"itemtypes","itype"},{"difficulties","difficulty"},{"areas","area"},
        {"locations","location"},{"stats","stat"},{"pstats","pstat"},{"filter_level","filter_levels"},{"levels","filter_levels"}
    };
    auto found=aliases.find(key);return found==aliases.end()?key:found->second;
}
inline std::string Trim(std::string value) {
    auto first=value.find_first_not_of(" \r\n\t"),last=value.find_last_not_of(" \r\n\t");
    return first==std::string::npos?"":value.substr(first,last-first+1);
}
inline bool Integer(const std::string& expression,int& result) {
    try { auto value=Trim(expression);size_t used=0;auto number=std::stoll(value,&used);if(used!=value.size()||number<(std::numeric_limits<int>::min)()||number>(std::numeric_limits<int>::max)())return false;result=int(number);return true;}catch(...){return false;}
}
inline bool Literal(const std::string& expression,std::string& result) {
    Document doc;
    if(doc.IsString(expression)){result=DecodeString(expression);return true;}
    int value=0;if(Integer(expression,value)){result=std::to_string(value);return true;}return false;
}
inline bool List(const std::string& expression,std::vector<std::string>& result) {
    std::string scalar;if(Literal(expression,scalar)){result.push_back(scalar);return true;}
    Document doc;if(!doc.Parse("return {value="+expression+"}"))return false;
    const auto* value=doc.Find("value");
    try {for(const auto& field:doc.Table(doc.TokenAt(value->valueBegin))){if(!field.key.empty()||!Literal(doc.Value(field),scalar))return false;result.push_back(scalar);}return true;}catch(...){return false;}
}
inline constexpr const char* Operators[] = {"Equals","At least","At most","Greater than","Less than","Does not equal","Between"};
inline constexpr const char* OperatorCode[] = {"==",">=","<=",">","<","~=","between"};
struct Value {
    bool editable = true, flag = true, exclude = false;
    std::vector<std::string> choices;
    std::string text;
    int operation = 0, number = 0, upper = 0, stat = 39, parameter = 0;
    std::array<float,4> color{1,1,1,1};
    float thickness = 2;
};
inline Value Decode(const Spec& spec,const std::string& raw) {
    Value value;std::string expression=Trim(raw);Document doc;
    if(spec.kind==Kind::Boolean){value.editable=expression=="true"||expression=="false";value.flag=expression!="false";}
    else if(spec.kind==Kind::Text){value.editable=doc.IsString(expression);if(value.editable)value.text=DecodeString(expression);}
    else if(spec.kind==Kind::Choices){
        if(expression.compare(0,3,"NOT")==0){value.exclude=true;expression=Trim(expression.substr(3));}
        value.editable=List(expression,value.choices);
    } else if(spec.kind==Kind::Number){
        value.editable=Integer(expression,value.number);
        if(!value.editable&&doc.IsString(expression)){
            auto text=DecodeString(expression);value.editable=false;
            for(int i=1;i<6;++i){std::string op=OperatorCode[i];if(text.compare(0,op.size(),op)==0&&Integer(text.substr(op.size()),value.number)){value.operation=i;value.editable=true;break;}}
            if(!value.editable&&text.size()>1&&(text.back()=='+'||text.back()=='-')){value.operation=text.back()=='+'?1:2;value.editable=Integer(text.substr(0,text.size()-1),value.number);}
        }
    } else if(spec.kind==Kind::Color){
        std::vector<std::string> parts;value.editable=List(expression,parts)&&parts.size()>=3&&parts.size()<=(std::string(spec.key)=="border"?5u:4u);
        for(size_t i=0;value.editable&&i<parts.size();++i){int component=0;value.editable=Integer(parts[i],component);if(i<4)value.color[i]=(std::clamp)(component,0,255)/255.f;else value.thickness=float(component);}
    } else if(spec.kind==Kind::Stat){
        value.editable=doc.Parse("return "+expression);
        bool index=false,amount=false;
        if(value.editable)for(const auto& field:doc.fields){auto text=doc.Value(field);
            if(field.key=="index"){index=Integer(text,value.stat);value.editable&=index;}
            else if(field.key=="param")value.editable&=Integer(text,value.parameter);
            else if(field.key=="op"){std::string op;value.editable&=Literal(text,op);bool found=false;for(int i=0;i<7;++i)if(op==OperatorCode[i]||(i==0&&op=="=")){value.operation=i;found=true;}value.editable&=found;}
            else if(field.key=="value"){
                amount=Integer(text,value.number);
                if(!amount){std::vector<std::string> parts;amount=List(text,parts)&&parts.size()==2&&Integer(parts[0],value.number)&&Integer(parts[1],value.upper);}
                value.editable&=amount;
            }else value.editable=false;
        }
        value.editable&=index&&amount;
    }
    return value;
}
inline std::string Encode(const Spec& spec,const Value& value) {
    if(!value.editable)throw std::runtime_error("Custom expression must be preserved");
    if(spec.kind==Kind::Boolean)return value.flag?"true":"false";
    if(spec.kind==Kind::Text)return Quote(value.text);
    if(spec.kind==Kind::Number)return value.operation==0?std::to_string(value.number):Quote(std::string(OperatorCode[std::clamp(value.operation,0,5)])+std::to_string(value.number));
    if(spec.kind==Kind::Choices){
        if(value.choices.empty())return "nil";
        if(std::string(spec.key)=="name_style"||std::string(spec.key)=="background_style")return Quote(value.choices[0]);
        bool numeric=std::string(spec.key)=="itype"||std::string(spec.key)=="area";
        std::string result=value.exclude?"NOT {":"{";
        for(size_t i=0;i<value.choices.size();++i){if(i)result+=", ";int id=0;result+=numeric&&Integer(value.choices[i],id)?std::to_string(id):Quote(value.choices[i]);}
        return result+"}";
    }
    if(spec.kind==Kind::Color){std::string result="{";for(size_t i=0;i<4;++i){if(i)result+=", ";result+=std::to_string(int(std::clamp(value.color[i],0.f,1.f)*255.f+0.5f));}if(std::string(spec.key)=="border")result+=", "+std::to_string(int(std::clamp(value.thickness,1.f,12.f)));return result+"}";}
    return "{index = "+std::to_string(value.stat)+", param = "+std::to_string(value.parameter)+", op = "+Quote(OperatorCode[std::clamp(value.operation,0,6)])+", value = "+(value.operation==6?"{"+std::to_string((std::min)(value.number,value.upper))+", "+std::to_string((std::max)(value.number,value.upper))+"}":std::to_string(value.number))+"}";
}
// Edit only a field's source span. Comments, local references, unknown fields,
// functions and other conditions keep their exact author source.
inline bool SetField(std::string& rule,const std::string& key,const std::string& expression,std::string& error) {
    const std::string prefix="return { rules = {",suffix="\n} }";
    Document doc;if(!doc.Parse(prefix+rule+suffix)||doc.rules.size()!=1){error="This rule needs the advanced editor.";return false;}
    try{
        const auto table=doc.Table(doc.TokenAt(doc.rules[0].valueBegin));
        std::vector<Patch> patches;
        for(const auto& field:table)if(Canonical(field.key)==key)patches.push_back({field.valueBegin,field.valueEnd,expression});
        if(patches.empty())patches.push_back({doc.rules[0].valueBegin+1,doc.rules[0].valueBegin+1,"\n            "+key+" = "+expression+","});
        auto changed=Document::Apply(doc.source,patches);rule=changed.substr(prefix.size(),changed.size()-prefix.size()-suffix.size());error.clear();return true;
    }catch(const std::exception&){error="This rule needs the advanced editor.";return false;}
}
inline std::vector<std::pair<std::string,std::string>> RuleFields(const std::string& rule) {
    Document doc;if(!doc.Parse("return {rules={"+rule+"\n}}")||doc.rules.size()!=1)return {};
    try{return doc.RuleFields(0);}catch(...){return {};}
}
inline std::vector<Option> Options(const std::string& key,const Catalog& catalog) {
    if(key=="codes")return catalog.items;
    if(key=="itype")return catalog.types;
    if(key=="area")return catalog.areas;
    std::vector<Option> result;
    auto add=[&](const char* name){result.push_back({name,name});};
    if(key=="quality")for(auto name:{"Inferior","Normal","Superior","Magic","Set","Rare","Unique","Crafted","Tempered"})add(name);
    if(key=="rarity")for(auto name:{"Normal","Exceptional","Elite"})add(name);
    if(key=="difficulty")for(auto name:{"Normal","Nightmare","Hell"})add(name);
    if(key=="location")result={{"On the ground","onground"},{"In inventory or storage","onplayer"},{"Equipped","equipped"},{"At a vendor","atvendor"}};
    if(key=="name_style")for(auto name:{"Rainbow","Rainbow Group","Cotton Candy","Frost Wave","Toxic Fog","Open Flames","Christmas","Rainbow Static","Rainbow Group Static","Cotton Candy Static","Frost Wave Static","Toxic Fog Static","Open Flames Static"})add(name);
    if(key=="background_style")for(auto name:{"Rainbow","Ocean Drift","Fading Ember","Midnight Memory","Verdant Bloom","Sapphire Sonnet","Tamarillo Glow"})add(name);
    return result;
}
inline std::vector<std::string> Columns(const std::string& line) {
    std::vector<std::string> result;size_t start=0;
    do{size_t end=line.find('\t',start);result.push_back(Trim(line.substr(start,end==std::string::npos?end:end-start)));if(end==std::string::npos)break;start=end+1;}while(true);
    return result;
}
// TXT catalogs are read on a worker through GameAssets, including custom rows.
inline void AddTable(Catalog& catalog,const std::string& table,const std::string& source) {
    std::istringstream stream(source);std::string line;if(!std::getline(stream,line))return;
    auto header=Columns(line);if(!header.empty()&&header[0].compare(0,3,"\xEF\xBB\xBF")==0)header[0].erase(0,3);
    auto column=[&](const char* name){auto it=std::find(header.begin(),header.end(),name);return it==header.end()?-1:int(it-header.begin());};
    int row=0;
    while(std::getline(stream,line)){
        auto cells=Columns(line);auto get=[&](const char* key){int index=column(key);return index>=0&&size_t(index)<cells.size()?cells[index]:std::string{};};
        if(cells.empty()||cells[0].empty()||cells[0]=="Expansion")continue;
        if(table=="uniqueitems"||table=="setitems"){
            auto name=get("index"),code=get(table=="uniqueitems"?"code":"item"),id=get("*ID");int index=row++;
            if(!id.empty()&&!Integer(id,index))continue;
            if(table=="uniqueitems"&&get("enabled")=="0")continue;
            if(!name.empty()&&!code.empty()&&index>=0)catalog.namedItems.push_back({name,code,table=="uniqueitems"?"Unique":"Set",index});
        }
        else if(table=="itemtypes"){
            auto name=get("ItemType"),code=get("Code");
            if(!name.empty()&&row>1)catalog.types.push_back({name,std::to_string(row)});
            if(!code.empty()){catalog.typeIds[code]=row;for(const char* parent:{"Equiv1","Equiv2"})if(!get(parent).empty())catalog.typeParents[code].push_back(get(parent));}
            ++row;
        }
        else if(table=="itemstatcost"){auto id=get("ID"),name=get("Stat");if(id.empty())id=get("*ID");int value=0;if(!name.empty()&&Integer(id,value)){std::replace(name.begin(),name.end(),'_',' ');catalog.stats.push_back({name,id});}}
        else if(table=="levels"){auto id=get("Id"),name=get("*name");if(name.empty())name=get("Name");int value=0;if(!name.empty()&&Integer(id,value)&&value>0)catalog.areas.push_back({name,id});}
        else{
            auto code=get("code"),name=get("name");
            if(!code.empty()&&!name.empty()){
                catalog.items.push_back({name,code});BaseItem base;base.name=name;
                for(const char* type:{"type","type2"})if(!get(type).empty())base.types.push_back(get(type));
                Integer(get("level"),base.level);Integer(get("gemsockets"),base.sockets);
                if(get("ultracode")==code&&get("normcode")!=code)base.tier=2;
                else if(get("ubercode")==code&&get("normcode")!=code)base.tier=1;
                catalog.bases[code]=std::move(base);
            }
        }
    }
}
inline void SortCatalog(Catalog& catalog) {
    std::stable_sort(catalog.namedItems.begin(),catalog.namedItems.end(),[](const NamedItem& a,const NamedItem& b){return a.label<b.label;});
    for(auto* list:{&catalog.items,&catalog.types,&catalog.stats,&catalog.areas}){
        std::stable_sort(list->begin(),list->end(),[](const Option& a,const Option& b){return a.label<b.label;});
        std::set<std::string> seen;list->erase(std::remove_if(list->begin(),list->end(),[&](const Option& option){return !seen.insert(option.value).second;}),list->end());
    }
}
}
