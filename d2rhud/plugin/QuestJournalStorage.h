#pragma once
#include "QuestJournalData.h"

namespace QuestJournal::Data {
// One writer per local character. The OS releases the lease after a crash;
// the persistent lock filename is not itself evidence of an active writer.
struct Store {
    HANDLE lease=INVALID_HANDLE_VALUE;
    std::filesystem::path path;
    History history;
    Store()=default;Store(const Store&)=delete;Store& operator=(const Store&)=delete;
    ~Store(){if(lease!=INVALID_HANDLE_VALUE)CloseHandle(lease);}

    static std::optional<History> Read(const std::filesystem::path& path){
        std::ifstream input(path,std::ios::binary|std::ios::ate);if(!input||input.tellg()<=0||input.tellg()>std::streamoff(MaxBytes))return {};
        std::string text(size_t(input.tellg()),'\0');input.seekg(0);if(!input.read(text.data(),text.size()))return {};return Decode(text,MaxQuests);
    }
    static std::optional<State> LegacyProgress(const std::filesystem::path& root,const Identity& identity,bool& invalid){
        bool found=false;State state;invalid=false;
        if(std::filesystem::exists(root))for(const auto& file:std::filesystem::recursive_directory_iterator(root,std::filesystem::directory_options::skip_permission_denied)){
            if(!file.is_regular_file()||file.path().extension()!=L".json"||file.path().parent_path()==root/"RMD-MP")continue;
            const auto stem=file.path().stem().string();const auto marker=stem.find('-');
            if(marker==std::string::npos||marker!=identity.name.size()*2)continue;
            std::string name;for(size_t n=0;n<marker;n+=2){unsigned value=0;auto parsed=std::from_chars(stem.data()+n,stem.data()+n+2,value,16);if(parsed.ec!=std::errc{}||parsed.ptr!=stem.data()+n+2){name.clear();break;}name+=char(value);}
            auto candidate=identity;candidate.name=name;if(!SameCharacter(candidate,identity))continue;
            auto saved=Read(file.path());if(!saved){invalid=true;continue;}
            if(!SameCharacter(saved->identity,identity))continue;
            state=Merge(state,Current(*saved));found=true;
        }
        return found?std::optional<State>{state}:std::optional<State>{};
    }
    bool Open(const std::filesystem::path& root,const Identity& identity,std::string& error){try{
        if(root.empty()||!SummonPersistence::Data::ValidIdentity(identity))throw std::runtime_error("Quest storage is unavailable.");
        path=root/"RMD-MP"/(QuestJournal::Data::Key(identity)+".json");std::filesystem::create_directories(path.parent_path());
        auto lock=path;lock+=L".lock";
        lease=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lease==INVALID_HANDLE_VALUE)throw std::runtime_error("Another game is using this character's quest journal, or its folder is not writable.");
        history.identity=identity;
        bool compact=false;
        if(std::filesystem::exists(path)){
            auto saved=Read(path);
            if(!saved||!SameCharacter(saved->identity,identity))throw std::runtime_error("This character's quest journal is unreadable. The original file was preserved.");
            history=std::move(*saved);history.identity=identity;
            compact=history.checkpoints.size()!=1;
            // Version 1 may contain a single checkpoint too.
            std::ifstream original(path,std::ios::binary);nlohmann::json document;original>>document;compact=compact||document.value("version",0)!=2;
            if(compact){auto backup=path;backup+=L".bak";if(!CopyFileW(path.c_str(),backup.c_str(),FALSE))throw std::runtime_error("Could not preserve the original quest journal.");}
        }else{
            // Import every matching old installation/timestamp checkpoint once.
            // Keep the furthest claims, not a save checksum or a definition hash.
            bool invalid=false;const auto state=LegacyProgress(root,identity,invalid);
            if(invalid&&!state)throw std::runtime_error("The existing quest journal could not be read. It was preserved; no fresh progress was substituted.");
            Append(history,{0,0,state.value_or(State{})});history.revision=1;
            if(!AtomicWrite(path,history,error))return false;
        }
        // Compact legacy files to one current state, with no checksum entries.
        Append(history,{0,0,Current(history)});
        if(compact){if(history.revision<UINT64_MAX)++history.revision;if(!AtomicWrite(path,history,error))return false;}
        return true;
    }catch(const std::exception& exception){error=exception.what();return false;}}

    bool Commit(State state,uint64_t revision,std::string& error){
        if(lease==INVALID_HANDLE_VALUE){error="The quest journal writer is not available.";return false;}
        // A delayed packet or an old host cannot roll back already claimed rewards.
        const auto old=Current(history);
        if(revision<history.revision||state.completed<old.completed){error="An older quest update was ignored; the current journal was retained.";return false;}
        if(revision==history.revision){if(state==old)return true;error="Conflicting quest update; waiting for the host's current state.";return false;}
        auto next=history;next.revision=revision;Append(next,{0,0,state});
        // One bounded recovery copy, never a new checksum-named file each save.
        auto backup=path;backup+=L".bak";
        if(!CopyFileW(path.c_str(),backup.c_str(),FALSE)){error="Could not back up the current quest journal.";return false;}
        if(!AtomicWrite(path,next,error))return false;
        history=std::move(next);return true;
    }
};
}
