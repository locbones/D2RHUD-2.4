#pragma once
#include "../json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Value-only, worker-loaded inventory artwork metadata. HD artwork is selected
// through the game's HD item mappings; the old invfile columns are DC6 names
// and must never be presented to an HD ImageWidget as sprite filenames.
namespace Marketplace::ItemVisuals {
using Read = std::function<std::vector<uint8_t>(const std::string&)>;
using ReadNames = std::function<std::vector<std::string>(const std::string&)>;
struct Visual {
    std::string filename;
    unsigned width=0,height=0; // Inventory cells, preserving the item aspect.
    bool exact=false; // False for a nominal price selector or missing variation.
    explicit operator bool() const {return !filename.empty()&&width&&height;}
};
namespace Detail {
struct Table {
    std::vector<uint8_t> bytes;size_t stride=0,count=0;
    Table(std::vector<uint8_t> source,size_t size,const std::string& name):bytes(std::move(source)),stride(size){
        uint32_t rows=0;if(bytes.size()>=4)std::memcpy(&rows,bytes.data(),4);count=rows;
        if(bytes.size()<4||count>100000||bytes.size()!=4+count*stride)
            throw std::runtime_error("Missing or unsupported 2.4 item artwork table: "+name);
    }
    const uint8_t* operator[](size_t i)const{return bytes.data()+4+i*stride;}
};
template<class T>T Field(const uint8_t* row,size_t offset){T value{};std::memcpy(&value,row+offset,sizeof(T));return value;}
inline std::string Text(const uint8_t* row,size_t at,size_t length){
    const auto start=reinterpret_cast<const char*>(row+at);size_t end=0;while(end<length&&start[end])++end;
    std::string text(start,end);while(!text.empty()&&text.back()==' ')text.pop_back();return text;
}
inline std::string Key(std::string_view name){
    // D2R 2.4's HD mapping-name normalizer (RVA 0x42a890), including
    // punctuation and CamelCase/number boundaries used by custom names.
    while(!name.empty()&&(name.back()==' '||name.back()==')'))name.remove_suffix(1);
    const auto digit=[](unsigned char c){return c>='0'&&c<='9';};
    const auto upper=[](unsigned char c){return c>='A'&&c<='Z';};
    std::string key;
    for(size_t i=0;i<name.size();++i){const unsigned char raw=name[i];unsigned char c=raw;if(upper(c))c+='a'-'A';
        if((c>='a'&&c<='z')||digit(c)){
            key.push_back(char(c));if(i+1<name.size()&&(upper(name[i+1])||(!digit(raw)&&digit(name[i+1]))))key.push_back('_');
        }else if(c!='\'')key.push_back('_');
    }
    return key;
}
// Assets are relative to the category directory. Custom mod paths commonly
// contain ../custom; normalize them without permitting escape from ui/items.
inline std::string Filename(std::string asset,const std::string& category){
    if(asset.empty()||asset.size()>400||asset.front()=='/'||asset.front()=='\\')return {};
    std::replace(asset.begin(),asset.end(),'\\','/');
    std::vector<std::string> parts{"items",category};
    if(asset.starts_with("items/")){parts.clear();parts.push_back("items");asset.erase(0,6);}
    for(size_t at=0;at<=asset.size();){const auto end=asset.find('/',at);auto part=asset.substr(at,end==std::string::npos?end:end-at);
        if(part==".."){if(parts.size()<=1)return {};parts.pop_back();}
        else if(!part.empty()&&part!="."){
            if(std::any_of(part.begin(),part.end(),[](unsigned char c){return c<32||c>=127||c==':'||c=='?'||c=='*'||c=='"'||c=='<'||c=='>'||c=='|';}))return {};
            parts.push_back(std::move(part));
        }
        if(end==std::string::npos)break;at=end+1;
    }
    if(parts.size()<3)return {};
    std::string result;for(const auto& part:parts){if(!result.empty())result+='/';result+=part;}
    // Native sprite loading supplies its own extension.
    for(const auto* extension:{".sprite",".json",".dc6"})if(result.ends_with(extension))return {};
    return result;
}
using Assets=std::map<std::string,std::array<std::string,3>>;
inline Assets LoadAssets(const Read& read,const char* file,bool named){
    const std::string path=std::string("hd/items/")+file+".json";const auto bytes=read(path);
    if(bytes.empty()||bytes.size()>8*1024*1024)throw std::runtime_error("Missing or oversized item artwork mapping: "+path);
    const auto json=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false);
    if(!json.is_array()||json.size()>100000)throw std::runtime_error("Unsupported item artwork mapping: "+path);
    Assets result;
    for(const auto& row:json){
        if(!row.is_object()||row.size()!=1)throw std::runtime_error("Invalid item artwork mapping row: "+path);
        const auto it=row.begin();if(it.key().empty()||!it.value().is_object())throw std::runtime_error("Invalid item artwork mapping identity: "+path);
        std::array<std::string,3> variants;
        for(size_t n=0;n<variants.size();++n){const char* key=named?std::array{"normal","uber","ultra"}[n]:"asset";const auto value=it.value().find(key);
            if(value!=it.value().end()){if(!value->is_string())throw std::runtime_error("Invalid item artwork mapping filename: "+path);variants[n]=value->get<std::string>();}
        }
        const auto existing=result.find(it.key());
        if(existing!=result.end()){if(existing->second!=variants)throw std::runtime_error("Conflicting item artwork mapping identity: "+path);}
        else result.emplace(it.key(),std::move(variants));
    }
    return result;
}
}
struct Base {
    std::string code,category;std::array<std::string,3> tiers;
    unsigned width=0,height=0,variations=0;
};
struct Named {std::string code,key;};
class Catalog {
    std::map<std::string,Base> bases_;
    std::vector<Named> uniques_,sets_;
    Detail::Assets baseAssets_,uniqueAssets_,setAssets_;
public:
    static Catalog Load(const Read& tableRead,const Read& assetRead,const ReadNames& readNames={}){
        Catalog result;Detail::Table types(tableRead("itemtypes"),0xe4,"itemtypes");
        result.baseAssets_=Detail::LoadAssets(assetRead,"items",false);
        result.uniqueAssets_=Detail::LoadAssets(assetRead,"uniques",true);
        result.setAssets_=Detail::LoadAssets(assetRead,"sets",true);
        for(const auto* name:{"weapons","armor","misc"}){
            Detail::Table rows(tableRead(name),0x1b4,name);
            for(size_t i=0;i<rows.count;++i){const auto row=rows[i];Base base;base.code=Detail::Text(row,0x80,4);if(base.code.empty())continue;
                base.category=std::string_view(name)=="weapons"?"weapon":name;base.width=row[0x116];base.height=row[0x117];
                if(base.code.size()>4||!base.width||base.width>16||!base.height||base.height>16)throw std::runtime_error("Invalid compiled item artwork dimensions.");
                for(size_t n=0;n<3;++n)base.tiers[n]=Detail::Text(row,0x84+n*4,4);
                const int type=Detail::Field<int16_t>(row,0x126);
                if(type<0||size_t(type)>=types.count)throw std::runtime_error("Invalid compiled item artwork type.");
                base.variations=types[size_t(type)][0x22];
                // HD mappings can expose all eight saved picture indices even
                // though legacy ItemTypes only contains six DC6 InvGfx slots.
                if(base.variations>8)throw std::runtime_error("Unsupported compiled item artwork variation count.");
                if(!result.bases_.emplace(base.code,std::move(base)).second)throw std::runtime_error("Duplicate compiled item artwork code.");
            }
        }
        for(const auto* name:{"uniqueitems","setitems"}){
            const bool unique=std::string_view(name)=="uniqueitems";Detail::Table rows(tableRead(name),unique?0x150:0x1bc,name);
            auto& output=unique?result.uniques_:result.sets_;output.resize(rows.count);const auto names=readNames?readNames(name):std::vector<std::string>{};
            for(size_t i=0;i<rows.count;++i){auto title=Detail::Text(rows[i],2,32);
                if(i<names.size()&&!title.empty()&&names[i].starts_with(title))title=names[i];
                output[i]={Detail::Text(rows[i],0x28,4),Detail::Key(title)};
            }
        }
        return result;
    }
    // picture is the saved zero-based inventory graphic index. -1 designates
    // a nominal payment selector whose roll/graphic is not known.
    Visual Resolve(const std::string& code,int quality=2,int index=-1,int picture=-1)const{
        const auto found=bases_.find(code);if(found==bases_.end()||picture< -1||picture>7)return {};
        const auto& base=found->second;Visual visual;visual.width=base.width;visual.height=base.height;
        bool namedOverride=false;std::string asset;
        if(quality==5||quality==7){const auto& identities=quality==7?uniques_:sets_;const auto& mappings=quality==7?uniqueAssets_:setAssets_;
            if(index>=0&&size_t(index)<identities.size()){
                const auto& named=identities[size_t(index)];int tier=0;
                if(named.code!=code){const auto original=bases_.find(named.code);if(original==bases_.end())return {};
                    if(code==original->second.tiers[1])tier=1;
                    else if(code==original->second.tiers[2])tier=2;
                    else return {};
                }
                const auto mapping=mappings.find(named.key);
                if(mapping!=mappings.end()){asset=mapping->second[size_t(tier)];namedOverride=!asset.empty();}
            }else if(index>=0)return {};
        }
        if(asset.empty()){const auto mapping=baseAssets_.find(code);if(mapping==baseAssets_.end())return {};asset=mapping->second[0];}
        visual.filename=Detail::Filename(asset,base.category);if(visual.filename.empty())return {};
        const auto baseMapping=baseAssets_.find(code);
        const bool genericArtwork=!namedOverride||(baseMapping!=baseAssets_.end()&&asset==baseMapping->second[0]);
        if(genericArtwork&&base.variations){
            // The native item renderer appends a one-based, count-clamped
            // decimal inventory variant to the mapped HD asset path. A named
            // ring/amulet that reuses this same path still needs its variant;
            // dedicated unique/set artwork keeps its exact mapped filename.
            const unsigned variation=picture<0?1:std::min(unsigned(picture)+1,base.variations);
            visual.filename+=std::to_string(variation);
        }
        visual.exact=picture>=0;return visual;
    }
    // Live cube/transmog items can retain the original unique/set identity
    // after their base code changes. Their inventory still has a valid base
    // graphic; don't suppress it or borrow an unrelated named item's picture.
    Visual ResolveInventory(const std::string& code,int quality,int index,int picture)const{
        auto visual=Resolve(code,quality,index,picture);
        if(visual)return visual;
        visual=Resolve(code,2,-1,picture);
        visual.exact=false;
        return visual;
    }
    size_t Size()const{return bases_.size();}
};
}
