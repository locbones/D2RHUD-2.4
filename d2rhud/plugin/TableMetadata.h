#pragma once
#include "../json.hpp"
#include <span>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace TableMetadata {
inline std::string Hash(std::span<const uint8_t> bytes) {
    uint64_t value=14695981039346656037ULL;
    for(const auto byte:bytes)value=(value^byte)*1099511628211ULL;
    std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();
}
inline std::string NameColumn(const std::string& table) {
    if(table=="itemstatcost")return "stat";
    if(table=="properties" || table=="itemtypes" || table=="weapons" || table=="armor" || table=="misc" || table=="gems")return "code";
    if(table=="skills")return "skill";
    if(table=="skilldesc")return "skilldesc";
    if(table=="states")return "state";
    if(table=="monstats")return "id";
    if(table=="superuniques")return "superunique";
    if(table=="treasureclassex")return "treasure class";
    if(table=="charstats" || table=="playerclass")return "class";
    if(table=="plrtype")return "name";
    if(table=="levels")return "name";
    return "index";
}
inline std::vector<std::string> Names(const std::vector<uint8_t>& text,const std::string& table) {
    std::string body(text.begin(),text.end());if(body.starts_with("\xef\xbb\xbf"))body.erase(0,3);
    std::istringstream input(body);std::string line;std::vector<std::string> result;
    if(!std::getline(input,line))return result;
    auto fields=[](std::string row){if(!row.empty()&&row.back()=='\r')row.pop_back();std::vector<std::string> cells;size_t start=0;for(size_t i=0;i<=row.size();++i)if(i==row.size()||row[i]=='\t'){cells.push_back(row.substr(start,i-start));start=i+1;}return cells;};
    auto header=fields(line);size_t column=header.size();
    for(size_t i=0;i<header.size();++i){auto key=header[i];std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});if(key==NameColumn(table))column=i;}
    if(column==header.size())return result;
    while(std::getline(input,line)){
        const auto cells=fields(line);if(cells.empty() || cells[0].empty() || cells[0]=="Expansion" || cells[0]=="expansion")continue;
        result.push_back(column<cells.size()?cells[column]:"");
    }
    return result;
}
inline const nlohmann::json* Entry(const nlohmann::json& manifest,const std::string& table,std::span<const uint8_t> bin) {
    if(bin.size()<4 || !manifest.is_object() || manifest.value("version",0)!=1 || !manifest.contains("tables"))return nullptr;
    uint32_t count=0;memcpy(&count,bin.data(),4);if(!count || count>1000000 || (bin.size()-4)%count)return nullptr;
    const auto& tables=manifest["tables"];if(!tables.is_object() || !tables.contains(table))return nullptr;
    const auto& entry=tables[table];
    if(!entry.is_object() || entry.value("bin_hash",std::string{})!=Hash(bin) || entry.value("bin_size",uint64_t(0))!=bin.size())return nullptr;
    for(const char* key:{"names","labels"})if(entry.contains(key)){
        if(!entry[key].is_array() || entry[key].size()!=count)return nullptr;
        for(const auto& value:entry[key])if(!value.is_string() || value.get_ref<const std::string&>().size()>4096)return nullptr;
    }
    return &entry;
}
}
