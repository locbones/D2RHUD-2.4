#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace HudItems {
struct SessionState {
    using Packet=std::pair<std::vector<uint8_t>,bool>;
    std::recursive_mutex mutex;
    uint64_t epoch=UINT64_MAX;
    bool showFiltered=false;
    std::unordered_map<uint32_t,int32_t> transmogs;
    std::unordered_map<uint32_t,std::string> names;
    std::vector<Packet> removed;
    std::unordered_map<uint32_t,Packet> readded;
    std::unordered_map<uint32_t,int> inventoryApplications;
    std::unordered_map<uint32_t,std::string> inventoryPaths;
    bool Synchronize(uint64_t value){
        std::lock_guard lock(mutex);if(epoch==value)return false;
        epoch=value;showFiltered=false;transmogs.clear();names.clear();removed.clear();readded.clear();inventoryApplications.clear();inventoryPaths.clear();return true;
    }
};
}
