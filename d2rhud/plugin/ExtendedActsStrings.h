#pragma once
#include "../json.hpp"
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace ExtendedActs::StringEdits {
using Json=nlohmann::ordered_json;
struct Table {
    Json rows;
    std::map<std::string,size_t> keys;
    std::map<int,std::string> ids;
    explicit Table(const std::string& text):rows(Json::parse(text)) {
        if(!rows.is_array())throw std::runtime_error("Act VI strings must be an array");
        for(size_t i=0;i<rows.size();++i) {
            const auto& row=rows[i];
            if(!row.is_object() || !row.contains("Key") || !row["Key"].is_string() ||
               !row.contains("id") || !row["id"].is_number_integer())throw std::runtime_error("Invalid Act VI string identity");
            const auto key=row["Key"].get<std::string>();const auto id=row["id"].get<int>();
            if(key.empty() || id<0 || !keys.emplace(key,i).second || !ids.emplace(id,key).second)
                throw std::runtime_error("Duplicate or invalid Act VI string identity");
        }
    }
};
struct Selection {std::string text;std::optional<std::string> archive;};
inline Selection Select(const std::string& active,const std::string& enabled,const std::string& disabled,
                        bool want,const std::optional<std::string>& saved=std::nullopt) {
    const Table current(active),six(enabled),five(disabled);
    std::map<std::string,int> owned;
    for(const auto& [key,index]:five.keys) {
        const auto found=six.keys.find(key);
        if(found==six.keys.end() || six.rows[found->second]!=five.rows[index])
            throw std::runtime_error("Act VI string templates disagree on an original entry");
    }
    for(const auto& [key,index]:six.keys)if(!five.keys.contains(key))owned.emplace(key,six.rows[index]["id"].get<int>());
    if(owned.empty())throw std::runtime_error("Act VI string template has no owned entries");
    auto validate=[&](const Table& table,bool archive) {
        for(const auto& [key,index]:table.keys) {
            const int id=table.rows[index]["id"].get<int>();
            const auto expected=six.keys.find(key);
            if((archive && !owned.contains(key)) || (expected!=six.keys.end() && six.rows[expected->second]["id"]!=id))
                throw std::runtime_error("Act VI string key changed its ID: "+key);
            const auto reserved=six.ids.find(id);
            if(reserved!=six.ids.end() && reserved->second!=key)
                throw std::runtime_error("Act VI string ID conflicts with: "+reserved->second);
        }
    };
    validate(current,false);
    std::optional<Table> prior;
    if(want && saved){prior.emplace(*saved);validate(*prior,true);}
    auto result=Json::array(),removed=Json::array();
    bool changed=false;
    for(const auto& row:current.rows) {
        if(!want && owned.contains(row["Key"].get<std::string>())){removed.push_back(row);changed=true;}
        else result.push_back(row);
    }
    if(want)for(const auto& row:six.rows) {
        const auto key=row["Key"].get<std::string>();
        if(!owned.contains(key) || current.keys.contains(key))continue;
        result.push_back(prior && prior->keys.contains(key)?prior->rows[prior->keys.at(key)]:row);changed=true;
    }
    // Keep all ordinary/event entries and their translations. Missing base
    // entries belong to the selected mod, not to the Act VI switcher.
    const auto& selected=want?six:five;
    const auto text=!changed?active:result==selected.rows?(want?enabled:disabled):result.dump(4)+'\n';
    return {text,removed.empty()?std::nullopt:std::optional<std::string>(removed.dump(4)+'\n')};
}
}
