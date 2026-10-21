#pragma once
#include "../GameAssets.h"
#include <array>
#include <map>
#include <functional>
#include <cstring>
#include <sstream>

namespace TerrorZoneTables {
using Resolve=std::function<int(const std::string&)>;
struct Drops {std::array<std::array<int,3>,3> base{},terror{};};
struct Database {std::map<int,Drops> monsters,superuniques;bool valid=false;};
inline std::vector<std::string> Fields(std::string line){
    if(!line.empty()&&line.back()=='\r')line.pop_back();std::vector<std::string> out;size_t start=0;
    for(size_t i=0;i<=line.size();++i)if(i==line.size()||line[i]=='\t'){out.push_back(line.substr(start,i-start));start=i+1;}return out;
}
inline bool Binary(std::map<int,Drops>& to,const std::vector<uint8_t>& bin,bool unique) {
    uint32_t count=0;if(bin.size()>=4)memcpy(&count,bin.data(),4);
    const size_t stride=unique?0x30:0x1dc;
    if(!count || count>65536 || bin.size()!=4+size_t(count)*stride)return false;
    for(size_t row=0;row<count;++row){
        const auto p=bin.data()+4+row*stride;uint16_t id=0;memcpy(&id,p,2);auto& out=to[int(id)];
        for(int diff=0;diff<3;++diff)for(int variant=0;variant<3;++variant){uint16_t tc=0;memcpy(&tc,p+(unique?0x2a+diff*2:0xba+diff*8+variant*2),2);out.base[diff][variant]=tc==0xffff?-1:int(tc);}
    }
    return true;
}
inline void Source(std::map<int,Drops>& to,const std::string& text,bool unique,bool compiled,const Resolve& resolve) {
    std::istringstream input(text);std::string line;if(!std::getline(input,line))return;
    auto header=Fields(line);if(!header.empty()&&header[0].starts_with("\xef\xbb\xbf"))header[0].erase(0,3);
    std::map<std::string,size_t> columns;for(size_t i=0;i<header.size();++i)columns[header[i]]=i;
    int ordinal=0;
    while(std::getline(input,line)){
        const auto fields=Fields(line);if(fields.empty() || fields[0].empty() || fields[0]=="Expansion" || fields[0]=="expansion")continue;
        auto get=[&](const std::string& key){const auto it=columns.find(key);return it!=columns.end()&&it->second<fields.size()?fields[it->second]:std::string{};};
        int id=ordinal++;if(unique && !get("hcIdx").empty())try{id=std::stoi(get("hcIdx"));}catch(...){continue;}
        if(compiled && !to.contains(id))continue;
        auto& record=to[id];
        for(int diff=0;diff<3;++diff){
            const std::string suffix=diff==1?"(N)":diff==2?"(H)":"";
            for(int variant=0;variant<3;++variant){
                if(!compiled){const auto name=get(unique?"TC"+suffix:"TreasureClass"+std::to_string(variant+1)+suffix);if(!name.empty())record.base[diff][variant]=resolve(name);}
                const auto name=get(unique?"TC"+suffix+" Desecrated":"TreasureClassDesecrated"+std::string(variant==1?"Champ":variant==2?"Unique":"")+suffix);
                if(!name.empty())record.terror[diff][variant]=resolve(name);
            }
        }
    }
}
inline Database Load(GameAssets& assets,const Resolve& resolve) {
    Database result;
    for(const bool unique:{false,true}){
        const std::string name=unique?"superuniques":"monstats";auto files=assets.ReadTable(name);auto& rows=unique?result.superuniques:result.monsters;
        const bool compiled=files.text.empty();
        if(compiled && !Binary(rows,files.binary,unique))continue;
        const auto text=compiled?assets.ReadTableOverlay(name):std::string(files.text.begin(),files.text.end());
        Source(rows,text,unique,compiled,resolve);
    }
    result.valid=!result.monsters.empty();return result;
}
inline int Adjust(const Database& db,int monster,int superunique,int difficulty,int variant,int current) {
    if(!db.valid || current<=0 || difficulty<0 || difficulty>2 || variant<0 || variant>2)return current;
    const Drops* drops=nullptr;
    if(superunique>=0){const auto it=db.superuniques.find(superunique);if(it!=db.superuniques.end())drops=&it->second;}
    if(!drops){const auto it=db.monsters.find(monster);if(it!=db.monsters.end())drops=&it->second;}
    if(!drops)return current;
    const int from=drops->base[difficulty][variant],to=drops->terror[difficulty][variant];
    if(from<=0 || to<=0)return current;
    const auto adjusted=int64_t(current)+to-from;return adjusted>0&&adjusted<=65535?int(adjusted):current;
}
}
