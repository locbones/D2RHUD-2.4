#pragma once
#include "SurvivorsData.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>

namespace CubeMap::Survivors {
// Explicit versioned layout, independent of native .d2s files and hero names.
#pragma pack(push,1)
struct RosterFile {
    uint64_t magic=0x31524f5356525332ULL;uint32_t version=2;
    std::array<Profile,Classes.size()> classes{};
    uint64_t checksum=0;
};
#pragma pack(pop)
inline uint64_t RosterChecksum(const RosterFile& file){
    uint64_t hash=14695981039346656037ULL;auto bytes=reinterpret_cast<const uint8_t*>(&file);
    for(size_t i=0;i<offsetof(RosterFile,checksum);++i)hash=(hash^bytes[i])*1099511628211ULL;
    return hash;
}
class Roster {
    std::filesystem::path path;
    RosterFile data{};
public:
    bool good=true;
    explicit Roster(std::filesystem::path file):path(std::move(file)){
        std::error_code error;const bool exists=std::filesystem::exists(path,error);
        if(error){good=false;return;}if(!exists)return;
        std::ifstream input(path,std::ios::binary);RosterFile saved{};
        // Upgrade the original seven-class roster without losing ranks or XP.
        // The first write retains the original file as the normal .bak backup.
        if(std::filesystem::file_size(path,error)==118 && !error){
            std::array<uint8_t,118> old{};
            if(!input.read(reinterpret_cast<char*>(old.data()),old.size())){good=false;return;}
            uint64_t magic=0,checksum=0;uint32_t version=0;
            memcpy(&magic,old.data(),8);memcpy(&version,old.data()+8,4);memcpy(&checksum,old.data()+110,8);
            uint64_t hash=14695981039346656037ULL;for(size_t n=0;n<110;++n)hash=(hash^old[n])*1099511628211ULL;
            if(magic!=data.magic || version!=1 || checksum!=hash){good=false;return;}
            for(size_t n=0;n<Classes.size();++n){memcpy(&saved.classes[n],old.data()+12+n*14,14);if(!Valid(saved.classes[n])){good=false;return;}}
            data=saved;return;
        }
        if(!input.read(reinterpret_cast<char*>(&saved),sizeof(saved)) || input.peek()!=EOF ||
           saved.magic!=data.magic || saved.version!=data.version || saved.checksum!=RosterChecksum(saved) ||
           !std::all_of(saved.classes.begin(),saved.classes.end(),[](const Profile& p){return Valid(p);})){good=false;return;}
        data=saved;
    }
    const Profile& Get(uint8_t id)const{return data.classes.at(id);}
    bool Store(uint8_t id,const Profile& profile){
        if(!good || id>=Classes.size() || !Valid(profile))return false;
        std::error_code error;const bool exists=std::filesystem::exists(path,error);if(error)return false;
        if(data.classes[id]==profile && exists)return true;
        auto next=data;next.classes[id]=profile;next.checksum=RosterChecksum(next);
        std::filesystem::create_directories(path.parent_path(),error);if(error)return false;
        auto temporary=path;temporary+=L".tmp";
        {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);if(!output.write(reinterpret_cast<const char*>(&next),sizeof(next)) || !output.flush())return false;}
        // Keep the last valid roster and replace atomically. A corrupt or
        // unknown-version input is never silently replaced with empty progress.
        auto backup=path;backup+=L".bak";
        if(exists && !CopyFileW(path.c_str(),backup.c_str(),FALSE))return false;
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
        data=next;return true;
    }
};
}
