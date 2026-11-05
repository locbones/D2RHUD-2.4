#pragma once
#include "../json.hpp"
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>

// Native layouts may be reformatted by editors or recolored by Pandemonium.
// Accept only those cosmetic differences; widget structure and act counts must
// still match one of the manifest-verified templates.
namespace ExtendedActs::LayoutEdits {
using Json=nlohmann::ordered_json;
inline Json Parse(const std::string& source){
    std::string clean;clean.reserve(source.size());bool quoted=false,escape=false;
    for(size_t i=0;i<source.size();++i){const char c=source[i];
        if(quoted){clean+=c;if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;continue;}
        if(c=='"'){quoted=true;clean+=c;continue;}
        if(c=='/'&&i+1<source.size()&&source[i+1]=='/'){
            clean+=' ';i+=2;while(i<source.size()&&source[i]!='\n'&&source[i]!='\r')++i;if(i<source.size())clean+=source[i];continue;
        }
        if(c=='/'&&i+1<source.size()&&source[i+1]=='*'){
            clean+=' ';i+=2;while(i+1<source.size()&&!(source[i]=='*'&&source[i+1]=='/'))++i;
            if(i+1>=source.size())throw std::runtime_error("Unclosed native layout comment");++i;continue;
        }
        clean+=c;
    }
    std::string text;text.reserve(clean.size());quoted=false;escape=false;
    for(size_t i=0;i<clean.size();++i){const char c=clean[i];
        if(!quoted&&c==','){size_t next=i+1;while(next<clean.size()&&std::isspace(static_cast<unsigned char>(clean[next])))++next;
            if(next<clean.size()&&(clean[next]=='}'||clean[next]==']'))continue;}
        text+=c;
        if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;}
        else if(c=='"')quoted=true;
    }
    auto result=Json::parse(text);if(!result.is_object())throw std::runtime_error("Native layout must be an object");return result;
}
inline std::string CanonicalSprite(std::string text){
    if(text.size()>=2&&text[text.size()-2]=='_'&&std::string("RBPYGD").find(text.back())!=std::string::npos)text.back()='R';
    return text;
}
inline void Canonicalize(Json& value){
    if(value.is_object())for(auto i=value.begin();i!=value.end();++i){
        if(i.key()=="filename"&&i.value().is_string())i.value()=CanonicalSprite(i.value().get<std::string>());
        else Canonicalize(i.value());
    }
    else if(value.is_array())for(auto& child:value)Canonicalize(child);
}
inline nlohmann::json Canonical(Json value){Canonicalize(value);return value;}
inline std::string Select(const std::string& active,const std::string& enabled,const std::string& disabled,bool want){
    const auto current=Parse(active),six=Parse(enabled),five=Parse(disabled);
    const auto normalized=Canonical(current),on=Canonical(six),off=Canonical(five);
    const bool isOn=normalized==on,isOff=normalized==off;
    if(!isOn&&!isOff)throw std::runtime_error("Native layout structure differs from both Act VI templates");
    if(want?isOn:isOff)return active; // Already selected: no writes or cosmetic loss.
    const auto& base=isOn?six:five;auto target=want?six:five;
    std::map<std::string,std::string> styles;
    auto collect=[&](auto&& self,const Json& original,const Json& changed)->void{
        if(original.is_string()&&original!=changed){
            const auto from=original.get<std::string>(),to=changed.get<std::string>();
            if(CanonicalSprite(from)!=CanonicalSprite(to))throw std::runtime_error("Unsupported native layout text edit");
            const auto [found,inserted]=styles.emplace(from,to);
            if(!inserted&&found->second!=to)throw std::runtime_error("Conflicting native layout color overrides");
        }else if(original.is_object())for(auto i=original.begin();i!=original.end();++i)self(self,i.value(),changed.at(i.key()));
        else if(original.is_array())for(size_t i=0;i<original.size();++i)self(self,original[i],changed[i]);
    };
    collect(collect,base,current);
    if(styles.empty())return want?enabled:disabled;
    auto apply=[&](auto&& self,Json& value)->void{
        if(value.is_object())for(auto i=value.begin();i!=value.end();++i){
            if(i.key()=="filename"&&i.value().is_string()){
                const auto found=styles.find(i.value().get<std::string>());if(found!=styles.end())i.value()=found->second;
            }else self(self,i.value());
        }else if(value.is_array())for(auto& child:value)self(self,child);
    };
    apply(apply,target);
    if(Canonical(target)!=(want?on:off))throw std::runtime_error("Native layout selection changed its structure");
    return target.dump(4)+'\n';
}
}
