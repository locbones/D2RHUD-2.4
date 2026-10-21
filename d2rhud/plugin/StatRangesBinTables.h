#pragma once
#include "GameAssets.h"
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <map>
#include <array>
#include <vector>
#include <string>
#include <cctype>

namespace StatRanges::Binary {
using Row=std::map<std::string,std::string>;
using Rows=std::vector<Row>;
inline std::string Lower(std::string s){for(auto& c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
inline std::string Alias(const char* kind,int id){return id<0 ? std::string{} : std::string("@")+kind+":"+std::to_string(id);}
inline Rows Text(const std::vector<uint8_t>& bytes){
    std::string text(bytes.begin(),bytes.end());if(text.starts_with("\xef\xbb\xbf"))text.erase(0,3);
    std::istringstream input(text);std::string line,value;std::vector<std::string> header;Rows rows;
    if(!std::getline(input,line))return rows;if(!line.empty()&&line.back()=='\r')line.pop_back();
    std::istringstream columns(line);while(std::getline(columns,value,'\t'))header.push_back(Lower(value));
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        Row row;std::istringstream values(line);size_t c=0;
        while(c<header.size()&&std::getline(values,value,'\t'))row[header[c++]]=value;
        if(!row.empty() && (row[header.front()]=="Expansion" || row[header.front()]=="expansion"))continue;
        rows.push_back(std::move(row));
    }return rows;
}
class Tables {
    GameAssets& assets;std::map<std::string,Rows> cache;
    bool preferBinary;
    std::ostringstream diagnostics;
    template<class T>static T Read(const uint8_t* row,size_t offset){T v{};memcpy(&v,row+offset,sizeof(v));return v;}
    static std::string Chars(const uint8_t* row,size_t offset,size_t length){
        std::string out(reinterpret_cast<const char*>(row+offset),length);const auto nul=out.find('\0');if(nul!=out.npos)out.resize(nul);
        while(!out.empty()&&out.back()==' ')out.pop_back();return out;
    }
    static std::string StatDefault(int id){
        // These IDs are part of the native property-function ABI (damage
        // functions 5/6/7, combined elemental damage and poison averaging).
        static constexpr std::pair<int,const char*> names[]{{17,"item_maxdamage_percent"},{18,"item_mindamage_percent"},{21,"mindamage"},{22,"maxdamage"},{48,"firemindam"},{49,"firemaxdam"},{50,"lightmindam"},{51,"lightmaxdam"},{52,"magicmindam"},{53,"magicmaxdam"},{54,"coldmindam"},{55,"coldmaxdam"},{57,"poisonmindam"},{58,"poisonmaxdam"},{59,"poisonlength"},{326,"poison_count"}};
        for(const auto& p:names)if(p.first==id)return p.second;
        return Alias("stat",id);
    }
    std::string Ref(const char* table,int id,const char* key,const char* alias){
        if(id<0)return {};const auto& rows=Get(table);if(size_t(id)>=rows.size())throw std::runtime_error(std::string("Invalid BIN reference to ")+table);
        const auto found=rows[id].find(key);return found!=rows[id].end()&&!found->second.empty()?Lower(found->second):Alias(alias,id);
    }
    std::string ItemCode(int id){
        if(id<0)return {};for(const char* name:{"weapons","armor","misc"}){const auto& rows=Get(name);if(size_t(id)<rows.size())return Lower(rows[id].at("code"));id-=int(rows.size());}
        throw std::runtime_error("Invalid BIN item-code reference");
    }
public:
    explicit Tables(GameAssets& a,bool compiled=false):assets(a),preferBinary(compiled){}
    std::string Diagnostics() const { return diagnostics.str(); }
    const Rows& Get(const std::string& name){
        auto old=cache.find(name);if(old!=cache.end())return old->second;
        auto files=assets.ReadTable(name,preferBinary);auto& rows=cache[name];
        const bool ignoredText=preferBinary && !files.binary.empty() && !files.text.empty();
        if(preferBinary && !files.binary.empty())files.text.clear();
        const auto& input=files.text.empty()?files.binary:files.text;
        uint64_t hash=14695981039346656037ull;for(auto byte:input)hash=(hash^byte)*1099511628211ull;
        const auto location=files.layer.u8string();
        diagnostics<<name<<(files.text.empty()?".bin":".txt")<<": "<<input.size()<<" bytes, fnv="<<std::hex<<hash<<std::dec
            <<", data="<<(location.empty()?"archive/fallback":std::string(reinterpret_cast<const char*>(location.data()),location.size()))<<'\n';
        if(ignoredText)diagnostics<<"  Using compiled BIN; loose TXT is not authoritative for stat ranges.\n";
        if(!files.text.empty()){rows=Text(files.text);return rows;}
        if(files.binary.empty())return rows;
        const std::map<std::string,size_t> sizes{{"itemstatcost",0x144},{"properties",0x2e},{"magicsuffix",0x8c},{"magicprefix",0x8c},{"automagic",0x8c},{"qualityitems",0x2c},{"charstats",0xd0},{"weapons",0x1b4},{"armor",0x1b4},{"misc",0x1b4},{"itemtypes",0xe4},{"runes",0xe0},{"gems",0xc0},{"uniqueitems",0x150},{"setitems",0x1bc},{"sets",0x128},{"skilldesc",0x128},{"skills",0x284}};
        const auto stride=sizes.at(name);const auto& bytes=files.binary;
        if(bytes.size()<4)throw std::runtime_error("Truncated "+name+".bin");const auto count=Read<uint32_t>(bytes.data(),0);
        if(count>100000 || bytes.size()!=4+size_t(count)*stride)throw std::runtime_error("Unsupported D2R 2.4 "+name+".bin layout");
        const auto names=assets.ReadTableNames(name,preferBinary);
        auto named=[&](size_t n,const char* kind){return n<names.size()&&!names[n].empty()?Lower(names[n]):Alias(kind,int(n));};
        auto stat=[&](int id){if(id<0)return std::string{};return id<int(names.size())&&!names[id].empty()?Lower(names[id]):StatDefault(id);};
        rows.resize(count);
        for(size_t i=0;i<count;++i){
            auto& out=rows[i];const auto row=bytes.data()+4+i*stride;
            auto number=[&](const std::string& key,int value){out[key]=std::to_string(value);};
            auto stringId=[&](const std::string& key,size_t off){out[key]=Alias("string",Read<uint16_t>(row,off));};
            auto mods=[&](size_t offset,size_t amount,const auto& keys){
                for(size_t n=0;n<amount;++n){const auto at=offset+n*16;const int id=Read<int32_t>(row,at);if(id<0)continue;
                    const auto k=keys(n);out[k[0]]=Ref("properties",id,"code","property");number(k[1],Read<int32_t>(row,at+4));number(k[2],Read<int32_t>(row,at+8));number(k[3],Read<int32_t>(row,at+12));}
            };
            if(name=="itemstatcost"){
                number("*id",Read<uint16_t>(row,0));out["stat"]=stat(Read<uint16_t>(row,0));
                number("descfunc",row[0x32]);number("dgrp",Read<uint16_t>(row,0x3a));number("dgrpfunc",Read<uint16_t>(row,0x3c));
                stringId("descstrpos",0x34);stringId("descstrneg",0x36);stringId("descstr2",0x38);stringId("dgrpstrpos",0x3e);stringId("dgrpstrneg",0x40);
                number("valshift",row[0x14]);number("op",row[0x50]);number("op param",row[0x51]);out["op stat1"]=stat(Read<int16_t>(row,0x54));
            }else if(name=="properties"){
                out["code"]=named(i,"property");
                for(int n=0;n<7;++n){const auto k=std::to_string(n+1);number("func"+k,row[0x18+n]);number("set"+k,row[2+n]);number("val"+k,Read<int16_t>(row,0xa+n*2));
                    const int id=Read<int16_t>(row,0x20+n*2);out["stat"+k]=Ref("itemstatcost",id,"stat","stat");}
            }else if(name=="magicsuffix"||name=="magicprefix"||name=="automagic"||name=="qualityitems"){
                mods(name=="qualityitems"?0xc:0x24,name=="qualityitems"?2:3,[](size_t n){const auto k=std::to_string(n+1);return std::array<std::string,4>{"mod"+k+"code","mod"+k+"param","mod"+k+"min","mod"+k+"max"};});
            }else if(name=="charstats"){
                out["class"]=Chars(row,0,16);stringId("strallskills",0x54);for(int n=0;n<3;++n)stringId("strskilltab"+std::to_string(n+1),0x56+n*2);
            }else if(name=="weapons"||name=="armor"||name=="misc"){
                out["code"]=Chars(row,0x80,4);number("gemapplytype",row[0x146]);out["type"]=Ref("itemtypes",Read<int16_t>(row,0x126),"code","type");out["type2"]=Ref("itemtypes",Read<int16_t>(row,0x128),"code","type");
            }else if(name=="itemtypes"){
                out["code"]=Chars(row,0,4);if(out["code"].empty())out["code"]=Alias("type",int(i));
                for(int n=0;n<2;++n){const int id=Read<int16_t>(row,4+n*2);if(id>0&&id<int(count)){out["equiv"+std::to_string(n+1)]=Chars(bytes.data()+4+size_t(id)*stride,0,4);}}
            }else if(name=="runes"){
                number("complete",row[0x40]);for(int n=0;n<6;++n)out["rune"+std::to_string(n+1)]=ItemCode(Read<int32_t>(row,0x58+n*4));
                for(int n=0;n<6;++n){const int id=Read<int16_t>(row,0x46+n*2);if(id>0)out["itype"+std::to_string(n+1)]=Ref("itemtypes",id,"code","type");}
                for(int n=0;n<3;++n){const int id=Read<int16_t>(row,0x52+n*2);if(id>0)out["etype"+std::to_string(n+1)]=Ref("itemtypes",id,"code","type");}
                mods(0x70,7,[](size_t n){const auto k=std::to_string(n+1);return std::array<std::string,4>{"t1code"+k,"t1param"+k,"t1min"+k,"t1max"+k};});
            }else if(name=="gems"){
                out["code"]=ItemCode(Read<int32_t>(row,0x28));
                for(int context=0;context<3;++context)mods(0x30+context*0x30,3,[&](size_t n){const auto k=std::string(context==0?"weapon":context==1?"helm":"shield")+"mod"+std::to_string(n+1);return std::array<std::string,4>{k+"code",k+"param",k+"min",k+"max"};});
            }else if(name=="uniqueitems"||name=="setitems"){
                number("*id",int(i));out["index"]=Chars(row,2,32);
                mods(name=="uniqueitems"?0x8c:0x88,name=="uniqueitems"?12:9,[](size_t n){const auto k=std::to_string(n+1);return std::array<std::string,4>{"prop"+k,"par"+k,"min"+k,"max"+k};});
                if(name=="setitems"){
                    out["set"]=Ref("sets",Read<uint16_t>(row,0x2c),"index","set");
                    mods(0x118,10,[](size_t n){const auto k=std::to_string(n/2+1)+(n%2?"b":"a");return std::array<std::string,4>{"aprop"+k,"apar"+k,"amin"+k,"amax"+k};});
                }
            }else if(name=="sets"){
                out["index"]=named(i,"set");
                mods(0x10,8,[](size_t n){const auto k=std::to_string(n/2+2)+(n%2?"b":"a");return std::array<std::string,4>{"pcode"+k,"pparam"+k,"pmin"+k,"pmax"+k};});
                mods(0x90,8,[](size_t n){const auto k=std::to_string(n+1);return std::array<std::string,4>{"fcode"+k,"fparam"+k,"fmin"+k,"fmax"+k};});
            }else if(name=="skilldesc"){
                out["skilldesc"]=named(i,"skilldesc");stringId("str name",8);
            }else if(name=="skills"){
                number("*id",Read<uint16_t>(row,0));out["skill"]=Chars(row,2,32);out["skilldesc"]=Ref("skilldesc",Read<int16_t>(row,0x1d4),"skilldesc","skilldesc");
            }
        }return rows;
    }
};
}
