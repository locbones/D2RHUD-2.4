#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <sstream>

namespace CubeVisual::Data {
constexpr uint16_t VisualFlag = 0x8000;
constexpr int IndexSlots = 4096;
struct Target {
    int kind = -1, index = -1; // base, unique, set
    bool Valid() const { return kind >= 0 && kind < 3 && index >= 0 && index < IndexSlots; }
    int Encode() const { return Valid() ? -(kind * IndexSlots + index + 1) : 0; }
    static Target Decode(int value) {
        if (value >= 0 || value < -3 * IndexSlots) return {};
        const int id = -value - 1;
        return {id / IndexSlots, id % IndexSlots};
    }
};
struct Modifier { int32_t property = -1; uint16_t param = 0; int16_t min = 0, max = 0; uint16_t chance = 0; };
struct Output {
    uint16_t flags = 0, base = 0, special = 0;
    uint8_t quality = 0, quantity = 0, type = 0, level = 0, playerLevel = 0, itemLevel = 0;
    uint16_t prefixes[3]{}, suffixes[3]{};
    std::array<Modifier,5> mods{};
    bool Visual() const { return (flags & VisualFlag) != 0; }
    bool PureVisual() const {
        if (flags != VisualFlag || type != 254 || special || quality || quantity ||
            !Target::Decode(-int(base)).Valid()) return false;
        for (auto id : prefixes) if (id) return false;
        for (auto id : suffixes) if (id) return false;
        for (const auto& mod : mods) if (mod.property >= 0) return false;
        return true;
    }
};
static_assert(sizeof(Output) == 0x54 && offsetof(Output,mods) == 0x18);
struct Recipe { std::array<uint8_t,0x4c> header{}; std::array<Output,3> outputs{}; };
static_assert(sizeof(Recipe) == 0x148);
inline std::string Trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==value.npos)return {};
    const auto last=value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first,last-first+1));
}
inline bool Equal(std::string_view a,std::string_view b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i) {
        auto lower=[](unsigned char c){return c>='A' && c<='Z' ? c+32 : c;};
        if(lower(a[i])!=lower(b[i]))return false;
    }
    return true;
}
constexpr uint16_t UseInputMarker=0x5500;
inline int UseInput(const Output& output){return output.type==254 && (output.special&0xfff8)==UseInputMarker ? int(output.special&7):1;}
inline bool IndexedUse(const Output& output){return output.type==254 && (output.special&0xfff8)==UseInputMarker;}
struct UseToken {bool present=false,valid=true,indexed=false;uint8_t input=1;std::string native;};
inline UseToken ParseUseItem(std::string_view raw){
    UseToken result;auto text=Trim(raw);
    if(text.size()>1 && text.front()=='"' && text.back()=='"')text=text.substr(1,text.size()-2);
    result.native=text;size_t begin=0;bool found=false;
    while(begin<=text.size()){
        const auto end=text.find(',',begin);auto token=Trim(std::string_view(text).substr(begin,end==text.npos?text.size()-begin:end-begin));
        if(token.size()>=7 && Equal(std::string_view(token).substr(0,7),"useitem")){
            result.present=true;if(found)result.valid=false;found=true;
            result.indexed=token.size()!=7;
            if(result.indexed){
                if(token.size()!=8 || token[7]<'1' || token[7]>'7')result.valid=false;
                else result.input=uint8_t(token[7]-'0');
                result.native="useitem"+(end==text.npos?std::string{}:text.substr(end));
                if(begin)result.valid=false; // The output command is the first token.
            }
        }
        if(end==text.npos)break;begin=end+1;
    }
    return result;
}
struct UseSource {std::array<UseToken,3> outputs;int enabled=0,inputs=0;};
struct Option { bool present=false, valid=true; std::string target; };
// The native parser stops on unknown modifiers. Remove our token before native
// parsing and reject mixed stat-changing options instead of silently ignoring them.
inline Option Parse(std::string_view input) {
    std::string text=Trim(input);
    if(text.size()>1 && text.front()=='"' && text.back()=='"' && text.find('"',1)==text.size()-1)
        text=text.substr(1,text.size()-2);
    std::vector<std::string> tokens;
    bool quoted=false;size_t begin=0;
    for(size_t i=0;i<=text.size();++i) {
        if(i<text.size() && text[i]=='"')quoted=!quoted;
        if(i==text.size() || (text[i]==',' && !quoted)) {tokens.push_back(Trim(std::string_view(text).substr(begin,i-begin)));begin=i+1;}
    }
    Option out;out.valid=!quoted;
    for(const auto& token:tokens) {
        auto split=token.find('=');
        const auto key=Trim(std::string_view(token).substr(0,split));
        if(Equal(key,"vis") || Equal(key,"visual")) {
            if(out.present || split==token.npos)out.valid=false;
            out.present=true;
            out.target=split==token.npos ? "" : Trim(std::string_view(token).substr(split+1));
            if(out.target.size()>1 && out.target.front()=='"' && out.target.back()=='"')out.target=out.target.substr(1,out.target.size()-2);
            if(out.target.empty())out.valid=false;
        }
    }
    if(out.present) for(const auto& token:tokens) {
        const auto split=token.find('=');
        const auto key=Trim(std::string_view(token).substr(0,split));
        if(!Equal(key,"vis") && !Equal(key,"visual") && (!ParseUseItem(token).present || !ParseUseItem(token).valid))out.valid=false;
    }
    return out;
}
inline bool HasMapToken(std::string_view text) {
    size_t begin=0;
    while(begin<text.size()) {
        const auto end=text.find(',',begin);
        auto token=Trim(text.substr(begin,end==text.npos ? text.size()-begin : end-begin));
        if(!token.empty() && token.front()=='"')token.erase(0,1);
        if(Equal(Trim(std::string_view(token).substr(0,token.find('='))),"map"))return true;
        if(end==text.npos)break;
        begin=end+1;
    }
    return false;
}
// Additional conditions are HUD annotations; the native BIN record stays 0x148 bytes.
struct Condition {int op=0;int32_t param=0,value=0;};
struct Conditions {
    std::array<Condition,3> tests{};
    bool valid=true,any=false;
    int enabled=0,inputs=0;
    bool Matches(const Recipe& recipe)const{
        int32_t param=0,value=0;memcpy(&param,recipe.header.data()+8,4);memcpy(&value,recipe.header.data()+12,4);
        return enabled==recipe.header[0] && inputs==recipe.header[0x10] && tests[0].op==recipe.header[4] &&
            (!tests[0].op || (tests[0].param==param && tests[0].value==value));
    }
};
inline std::optional<Conditions> ParseConditions(const std::vector<std::string>& header,const std::vector<std::string>& row){
    auto cell=[&](std::string_view key){for(size_t i=0;i<header.size();++i)if(Equal(header[i],key))return i<row.size()?row[i]:std::string{};return std::string{};};
    bool present=false;for(const char* key:{"op2","param2","value2","op3","param3","value3","oplogic"})present|=!cell(key).empty();
    if(!present)return {};
    Conditions out;
    auto number=[&](const std::string& text){int32_t value=0;if(text.empty())return value;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())out.valid=false;
        return value;
    };
    out.enabled=number(cell("enabled"));out.inputs=number(cell("numinputs"));
    for(size_t i=0;i<3;++i){const auto suffix=i?std::to_string(i+1):std::string{};auto& test=out.tests[i];
        test.op=number(cell("op"+suffix));test.param=number(cell("param"+suffix));test.value=number(cell("value"+suffix));
        if(test.op<0 || test.op>28)out.valid=false;
    }
    const auto logic=cell("oplogic");out.any=Equal(logic,"OR");
    if(!logic.empty() && !out.any && !Equal(logic,"AND"))out.valid=false;
    return out;
}
struct SourceRecipe { std::array<Option,3> outputs{}; std::array<std::string,3> rawOutputs{}; int enabled=0, inputs=0; bool matches=true; };
struct SourceTable { size_t rows=0;std::map<size_t,SourceRecipe> visuals;std::map<size_t,Conditions> conditions;std::map<size_t,UseSource> useItems; };
inline SourceTable ParseSource(const std::string& text) {
    SourceTable result;std::istringstream stream(text);std::string line;
    auto fields=[](const std::string& row) {
        std::vector<std::string> values;size_t begin=0;
        for(size_t i=0;i<=row.size();++i)if(i==row.size() || row[i]=='\t') {values.push_back(Trim(std::string_view(row).substr(begin,i-begin)));begin=i+1;}
        return values;
    };
    if(!std::getline(stream,line))return result;
    auto header=fields(line);
    std::array<int,3> columns{-1,-1,-1};int enabled=-1,inputs=-1;
    for(size_t i=0;i<header.size();++i) {
        if(Equal(header[i],"output"))columns[0]=int(i);
        if(Equal(header[i],"output b"))columns[1]=int(i);
        if(Equal(header[i],"output c"))columns[2]=int(i);
        if(Equal(header[i],"enabled"))enabled=int(i);
        if(Equal(header[i],"numinputs"))inputs=int(i);
    }
    if(columns[0]<0 || enabled<0 || inputs<0)return result;
    while(std::getline(stream,line)) {
        const size_t row=result.rows++;auto values=fields(line);SourceRecipe recipe;bool visual=false;UseSource use;bool indexed=false;
        auto number=[&](int index){if(index<0 || size_t(index)>=values.size())return 0;try{return std::stoi(values[index]);}catch(...){return 0;}};
        recipe.enabled=number(enabled);recipe.inputs=number(inputs);use.enabled=recipe.enabled;use.inputs=recipe.inputs;
        if(auto conditions=ParseConditions(header,values))result.conditions.emplace(row,*conditions);
        for(size_t i=0;i<3;++i)if(columns[i]>=0 && size_t(columns[i])<values.size()) {
            recipe.rawOutputs[i]=values[columns[i]];use.outputs[i]=ParseUseItem(recipe.rawOutputs[i]);indexed|=use.outputs[i].indexed;
            recipe.outputs[i]=Parse(values[columns[i]]);visual|=recipe.outputs[i].present;
            visual|=HasMapToken(recipe.rawOutputs[i]);
        }
        if(indexed)result.useItems.emplace(row,std::move(use));
        if(visual)result.visuals.emplace(row,std::move(recipe));
    }
    return result;
}
}
