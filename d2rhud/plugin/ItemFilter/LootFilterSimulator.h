#pragma once
#include "LootFilterPreview.h"

namespace LootFilterConfig::Simulator {
using Preview::Sample;
inline std::vector<Builder::Option> Items(const Builder::Catalog& catalog){
    std::vector<Builder::Option> items;items.reserve(catalog.items.size()+catalog.namedItems.size());
    for(const auto& item:catalog.items)items.push_back({item.label+" ["+item.value+"]","base:"+item.value});
    for(const auto& item:catalog.namedItems)items.push_back({item.label+" ("+item.quality+")","named:"+item.quality+":"+std::to_string(item.index)});
    std::stable_sort(items.begin(),items.end(),[](const Builder::Option& a,const Builder::Option& b){return a.label<b.label;});return items;
}
inline bool Select(const Builder::Catalog& catalog,const std::string& selection,Sample& sample){
    std::string code;const Builder::NamedItem* named=nullptr;
    if(selection.compare(0,5,"base:")==0)code=selection.substr(5);
    else for(const auto& item:catalog.namedItems)if(selection=="named:"+item.quality+":"+std::to_string(item.index)){named=&item;code=item.code;break;}
    const auto base=catalog.bases.find(code);
    if(code.empty()||(base==catalog.bases.end()&&std::none_of(catalog.items.begin(),catalog.items.end(),[&](const auto& item){return item.value==code;})))return false;
    sample={};sample.code=code;sample.name=code;sample.index=-1;sample.level=85;
    if(base!=catalog.bases.end()){
        sample.name=base->second.name;sample.tier=base->second.tier;sample.maximum=base->second.sockets;
        sample.types=Preview::Types(catalog,base->second.types);
    }else for(const auto& item:catalog.items)if(item.value==code){sample.name=item.label;break;}
    if(named){sample.name=named->label;sample.index=named->index;sample.quality=named->quality=="Unique"?7:5;}
    return true;
}
inline std::string Condition(const std::string& key){
    if(key=="codes")return "Different item code";
    if(key=="filter_levels")return "Different filter level";
    if(key=="location")return "Different item location";
    if(key=="stat")return "Item stats do not match";
    if(key=="pstat")return "Player stats do not match";
    const auto* spec=Builder::FindSpec(key);return spec?std::string(spec->label)+" does not match":key;
}
inline std::string Action(const std::string& key){
    static const std::map<std::string,std::string> labels{
        {"hide","Hide item"},{"hide = false","Hide off"},{"hide_mismatches","Hide mismatches"},
        {"hide_mismatches = false","Do not hide mismatches"},{"notify","Chat notification"},{"audio","Sound / speech"},
        {"print","Chat message"},{"print = false","No chat message"},{"background","Background color"},{"border","Border"},
        {"name_override","Replace name"},{"prefix","Name prefix"},{"suffix","Name suffix"},
        {"prefix_desc","Tooltip text"},{"suffix_desc","Tooltip text"},{"name_style","Animated name"},{"background_style","Animated background"}
    };const auto found=labels.find(key);return found==labels.end()?key:found->second;
}
}
