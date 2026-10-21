#pragma once
#include "LootFilterRuleBuilder.h"
#include "../GameAssets.h"
#include "../../json.hpp"
#include <cstring>
#include <span>

// D2R 2.4 compiled catalog records. These readers never use the runtime TXT
// name linkers, which are empty when the game is started without -txt.
namespace LootFilterConfig::Builder::Tables {
struct Binary {
    std::span<const uint8_t> bytes;size_t stride=0,count=0;
    Binary(std::span<const uint8_t> source,size_t size):bytes(source),stride(size){
        uint32_t rows=0;if(bytes.size()<4)throw std::runtime_error("Truncated catalog BIN table");
        std::memcpy(&rows,bytes.data(),4);count=rows;
        if(count>1000000 || bytes.size()!=4+count*stride)throw std::runtime_error("Unsupported catalog BIN layout");
    }
    const uint8_t* operator[](size_t i)const{return bytes.data()+4+i*stride;}
};
template<class T>T Field(const uint8_t* row,size_t at){T result{};std::memcpy(&result,row+at,sizeof(T));return result;}
inline std::string Text(const uint8_t* row,size_t at,size_t length){
    const auto start=reinterpret_cast<const char*>(row+at);size_t end=0;while(end<length&&start[end])++end;
    std::string result(start,end);while(!result.empty()&&result.back()==' ')result.pop_back();return result;
}
inline std::string NamedItemKey(const uint8_t* row,size_t index,const std::vector<std::string>& names){
    const auto stored=Text(row,2,32);
    return !stored.empty()&&index<names.size()&&names[index].starts_with(stored)?names[index]:stored;
}
struct Labels {
    std::map<int,std::string> ids;
    std::map<std::string,std::string> keys;
    std::string ById(int id,const std::string& fallback)const{auto i=ids.find(id);return i==ids.end()?fallback:i->second;}
    std::string ByKey(const std::string& key)const{auto i=keys.find(key);return i==keys.end()?key:i->second;}
};
inline Labels LoadLabels(GameAssets& assets){
    Labels labels;
    for(const char* name:{"item-names","item-nameaffixes","item-gems","item-runes","levels"}){
        const auto bytes=assets.Read(std::string("local/lng/strings/")+name+".json");
        const auto source=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false);if(!source.is_array())continue;
        for(const auto& row:source)if(row.is_object()){
            const auto title=row.value("enUS",std::string{});if(title.empty())continue;
            if(row.contains("id")&&row["id"].is_number_integer())labels.ids[row["id"].get<int>()]=title;
            if(row.contains("Key")&&row["Key"].is_string())labels.keys[row["Key"].get<std::string>()]=title;
        }
    }
    return labels;
}
inline std::string TypeCode(const Catalog& catalog,int id){
    for(const auto& [code,index]:catalog.typeIds)if(index==id)return code;return {};
}
inline void AddBinary(Catalog& catalog,const std::string& table,std::span<const uint8_t> bytes,const std::vector<std::string>& names={},const Labels& labels={}){
    if(table=="itemtypes"){
        Binary rows(bytes,0xe4);
        for(size_t i=0;i<rows.count;++i){const auto row=rows[i];const auto code=Text(row,0,4);
            auto name=i<names.size()?names[i]:std::string{};if(name.empty())name=code.empty()?"Item type "+std::to_string(i):code;
            if(i>1)catalog.types.push_back({name,std::to_string(i)});
            if(!code.empty()){
                catalog.typeIds[code]=int(i);
                for(size_t at:{size_t(4),size_t(6)}){const int parent=Field<int16_t>(row,at);if(parent>0&&size_t(parent)<rows.count){const auto parentCode=Text(rows[parent],0,4);if(!parentCode.empty())catalog.typeParents[code].push_back(parentCode);}}
            }
        }
    }else if(table=="itemstatcost"){
        Binary rows(bytes,0x144);
        for(size_t i=0;i<rows.count;++i){const int id=Field<uint16_t>(rows[i],0);auto name=i<names.size()?names[i]:std::string{};
            if(name.empty())name="Stat "+std::to_string(id);else std::replace(name.begin(),name.end(),'_',' ');
            catalog.stats.push_back({name,std::to_string(id)});
        }
    }else if(table=="levels"){
        Binary rows(bytes,0x184);
        for(size_t i=0;i<rows.count;++i){const int id=Field<uint32_t>(rows[i],0);if(id<=0)continue;
            auto name=Text(rows[i],0xf9,40);if(name.empty()&&i<names.size())name=names[i];if(name.empty())name="Area "+std::to_string(id);
            catalog.areas.push_back({labels.ByKey(name),std::to_string(id)});
        }
    }else if(table=="uniqueitems"||table=="setitems"){
        const bool unique=table=="uniqueitems";Binary rows(bytes,unique?0x150:0x1bc);
        for(size_t i=0;i<rows.count;++i){const auto row=rows[i];if(unique&&!(Field<uint32_t>(row,0x2c)&1))continue;
            const auto name=NamedItemKey(row,i,names),code=Text(row,0x28,4);if(name.empty()||code.empty())continue;
            // The leading runtime ID slot is zero in on-disk records. Native
            // unique/set indices are the compiled row ordinals.
            catalog.namedItems.push_back({labels.ByKey(name),code,unique?"Unique":"Set",int(i)});
        }
    }else if(table=="weapons"||table=="armor"||table=="misc"){
        Binary rows(bytes,0x1b4);
        for(size_t i=0;i<rows.count;++i){const auto row=rows[i];const auto code=Text(row,0x80,4);if(code.empty())continue;
            BaseItem base;base.name=labels.ById(Field<uint16_t>(row,0xfc),labels.ByKey(code));base.level=row[0x105];base.sockets=row[0x140];
            for(size_t at:{size_t(0x126),size_t(0x128)}){auto type=TypeCode(catalog,Field<int16_t>(row,at));if(!type.empty())base.types.push_back(type);}
            if(Text(row,0x84,4)!=code){if(Text(row,0x8c,4)==code)base.tier=2;else if(Text(row,0x88,4)==code)base.tier=1;}
            catalog.items.push_back({base.name,code});catalog.bases[code]=std::move(base);
        }
    }
}
inline Catalog Load(GameAssets& assets){
    Catalog catalog;const auto labels=LoadLabels(assets);
    // Types precede base items so both TXT and BIN inheritance resolve codes.
    for(const char* table:{"itemtypes","weapons","armor","misc","itemstatcost","levels","uniqueitems","setitems"}){
        const auto files=assets.ReadTable(table);
        if(!files.text.empty())AddTable(catalog,table,std::string(files.text.begin(),files.text.end()));
        else if(!files.binary.empty())AddBinary(catalog,table,files.binary,std::string_view(table)=="itemtypes"?assets.ReadTableLabels(table):assets.ReadTableNames(table),labels);
    }
    SortCatalog(catalog);catalog.loaded=true;
    if(catalog.items.empty())catalog.status="Item names are unavailable. Category and quality conditions can still be used.";
    return catalog;
}
}
