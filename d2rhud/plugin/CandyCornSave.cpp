#include "CandyCornSave.h"
#include "CandyCornSaveTables.h"
#include "CandyCornSaveStore.h"
#include "../Pattern.h"
#include "../d2/Reclass.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <deque>
#include <fstream>
#include <mutex>

namespace CandyCornSave {
namespace {
using namespace CandyCornSaveData;
using Load=int(__fastcall*)(D2GameStrc*,D2ClientStrc*,uint8_t*,int32_t,D2UnitStrc**);
Load originalLoad=nullptr;
std::atomic<bool> installed{false};
std::mutex mutex;
std::string status="Candy Corn conversion is not initialized.";
std::filesystem::path directory;
std::deque<Notification> notices;
struct Signature {uint32_t rva,size;uint64_t hash;};
constexpr std::array<Signature,7> signatures{{
    {0x31d540,32,0x9783e7dc39ce527eULL}, // Synchronous current-format reader, no buffer ownership.
    {0x2934ef,32,0x11ea0b69a437d299ULL}, // Caller loads first native descriptor pointer and length.
    {0x31a580,32,0xde2c95873e427480ULL},
    {0x14badb0,511*8,0x3a874274e5683ee4ULL}, // Complete native Huffman code tree.
    {0x1f0290,32,0x685839ffbd0f1ea4ULL},
    {0x1fe680,32,0x279e05c3ab40a8eaULL},
    {0x203c58,32,0xa2b71f88e5ec1ef0ULL},
}};
template<class T>T Native(uint32_t rva){return reinterpret_cast<T>(Pattern::Address(rva));}
bool Read(const void* at,void* out,size_t size){SIZE_T count=0;return at && ReadProcessMemory(GetCurrentProcess(),at,out,size,&count) && count==size;}
template<class T>T Field(const void* at,size_t offset=0){T out{};if(at)Read(static_cast<const uint8_t*>(at)+offset,&out,sizeof(out));return out;}
bool Supported(){
    std::array<uint8_t,511*8> bytes{};
    for(auto s:signatures)if(!Read(Native<void*>(s.rva),bytes.data(),s.size) || Storage::Hash({bytes.data(),s.size})!=s.hash)return false;
    return true;
}
void Report(std::string value){
    std::lock_guard lock(mutex);status=std::move(value);
    OutputDebugStringA(("D2RHUD Candy Corn: "+status+"\n").c_str());
    if(!directory.empty())try{std::filesystem::create_directories(directory);std::ofstream out(directory/L"last-conversion.txt",std::ios::binary|std::ios::trunc);out<<status<<'\n';}catch(...){}
}
void Notice(std::string title,std::string message,bool error=false){
    std::lock_guard lock(mutex);if(notices.size()==8)notices.pop_front();notices.push_back({std::move(title),std::move(message),error});
}
bool ReadTables(Tables& tables){
    const auto items=Field<const uint8_t*>(Native<void*>(0x1ca0390));
    const auto itemCount=Field<uint32_t>(Native<void*>(0x1ca0398));
    const auto stats=Field<const uint8_t*>(Native<void*>(0x1c9fb78));
    const auto statCount=Field<uint32_t>(Native<void*>(0x1c9fb80));
    const auto types=Field<const uint32_t*>(Native<void*>(0x1c9fc10));
    const auto typeCount=Field<uint32_t>(Native<void*>(0x1c9fbf8));
    const auto typeStride=Field<uint32_t>(Native<void*>(0x1c9fc08));
    if(!items || !stats || !types || !itemCount || itemCount>100000 || !statCount || statCount>4096 ||
        typeCount<=50 || typeCount>4096 || typeStride!=(typeCount+31)/32)return false;
    std::vector<uint8_t> itemCopy(size_t(itemCount)*0x1b4),statCopy(size_t(statCount)*0x144);
    std::vector<uint32_t> typeCopy(size_t(typeCount)*typeStride);
    if(!Read(items,itemCopy.data(),itemCopy.size()) || !Read(stats,statCopy.data(),statCopy.size()) ||
        !Read(types,typeCopy.data(),typeCopy.size()*4) || !DecodeTables(itemCopy,statCopy,typeCopy,typeCount,typeStride,tables))return false;
    const auto candy=std::find_if(tables.items.begin(),tables.items.end(),[](const auto& row){return row.code==std::array<char,4>{'E','v','1','1'};});
    return candy!=tables.items.end() && candy->stackable && !candy->armor && !candy->weapon && !candy->gold;
}
struct Context {
    D2ClientStrc* client=nullptr;D2UnitStrc** output=nullptr;
    uint8_t* source=nullptr;int32_t size=0;
    std::vector<uint8_t> converted;
    size_t count=0;bool used=false,completed=false;
    bool readerReturned=false;int readerResult=0;
    std::string character;
    std::filesystem::path backup;
    bool blocked=false;
};
thread_local Context* active=nullptr;
std::string DisplayPath(const std::filesystem::path& path){const auto text=path.u8string();return {reinterpret_cast<const char*>(text.data()),text.size()};}
void Complete(Context& c,bool success,int nativeResult);
bool Prepare(Context& c,D2ClientStrc* client,uint8_t* source,int32_t size,D2UnitStrc** output){
    if(!source || size<0x2ff || size>8*1024*1024)return false;
    std::vector<uint8_t> bytes(size_t(size),uint8_t(0));if(!Read(source,bytes.data(),bytes.size()))return false;
    Tables tables;if(!ReadTables(tables))return false;
    auto result=Convert(bytes,tables);
    if(result.status==CandyCornSaveData::Status::Unchanged)return false;
    if(result.status!=CandyCornSaveData::Status::Converted){Report("Save left unchanged: "+result.message);return false;}
    c.client=client;c.output=output;c.source=source;c.size=size;c.count=result.converted;c.converted=std::move(result.bytes);
    const auto name=std::find(bytes.begin()+0x10b,bytes.begin()+0x148,uint8_t(0));
    c.character=std::string(bytes.begin()+0x10b,name);if(c.character.empty())c.character="Character";
    std::string error;
    if(!Storage::Backup(directory,bytes,c.backup,error)){
        c.blocked=true;
        Report("Candy Corn conversion refused because the original save could not be backed up: "+error);
        Notice("Candy Corn conversion paused","The original save could not be backed up. Free disk space or check folder access, then try joining again.",true);
        return false;
    }
    Notice("Updating Candy Corn",c.character+": your Candy Corn are being converted for the new update.");
    Report(c.character+": supplying converted save to the native reader; Candy Corn="+std::to_string(c.count)+
        "; original bytes="+std::to_string(c.size)+"; converted bytes="+std::to_string(c.converted.size())+
        ". Original save backup: "+DisplayPath(c.backup));
    return true;
}
int __fastcall LoadHook(D2GameStrc* game,D2ClientStrc* client,uint8_t* bytes,int32_t size,D2UnitStrc** output){
    if(!installed.load())return originalLoad(game,client,bytes,size,output);
    // Conversion belongs to this reader, not to the optional map/summon load
    // wrapper. Its actual input is authoritative; never guess a descriptor or
    // assume an outer caller will use the same output slot.
    Context local;
    auto context=active && !active->used && active->client==client ? active : &local;
    context->used=true;
    bool ready=false;
    try{ready=Prepare(*context,client,bytes,size,output);}
    catch(const std::exception& e){
        // Once legacy Candy Corn were positively identified, do not send the
        // incompatible bytes to the game if backup/preparation cannot finish.
        context->blocked=context->count!=0;
        Report(std::string("Candy Corn conversion could not prepare the save: ")+e.what());
    }
    if(context->blocked){if(output)*output=nullptr;return 6;} // Native bad-save result; incompatible bytes never parsed.
    if(!ready)return originalLoad(game,client,bytes,size,output);
    // Do not replace native descriptor pointers: the OUTER loader frees them
    // before it returns. Only this leaf borrows our mutable, validated copy.
    const int result=originalLoad(game,client,context->converted.data(),int32_t(context->converted.size()),output);
    context->readerResult=result;context->readerReturned=true;
    if(context==&local)Complete(*context,result==0,result);
    return result;
}
}
struct LoadScope::State {Context context;Context* previous=nullptr;};
void Initialize(const char* mod){
    if(installed.load())return;
    if(!mod || _stricmp(mod,"RMD-MP")){Report("Candy Corn conversion applies to RMD-MP only.");return;}
    wchar_t local[32768]{},exe[32768]{};
    const auto localSize=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768),exeSize=GetModuleFileNameW(nullptr,exe,32768);
    if(!localSize || localSize>=32768 || !exeSize || exeSize>=32768){Report("Candy Corn conversion cannot resolve its backup folder.");return;}
    const auto root=std::filesystem::path(exe).parent_path().wstring();
    directory=std::filesystem::path(local)/L"D2RHUD"/L"SaveConversions"/Storage::Hex(Storage::Hash({reinterpret_cast<const uint8_t*>(root.data()),root.size()*sizeof(wchar_t)}))/L"RMD-MP";
    if(!Supported()){Report("Candy Corn conversion unavailable: executable signatures do not match.");return;}
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED){Report("Candy Corn conversion could not initialize its load hook.");return;}
    const auto target=Native<void*>(0x31d540);
    if(MH_CreateHook(target,reinterpret_cast<void*>(LoadHook),reinterpret_cast<void**>(&originalLoad))!=MH_OK){Report("Candy Corn conversion load hook is unavailable.");return;}
    if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);originalLoad=nullptr;Report("Candy Corn conversion load hook could not be enabled.");return;}
    installed.store(true);Report("Automatic Candy Corn save conversion is ready; original saves are backed up before conversion.");
}
void Uninstall(){if(installed.exchange(false)){const auto target=Native<void*>(0x31d540);MH_DisableHook(target);MH_RemoveHook(target);}originalLoad=nullptr;}
std::string Status(){std::lock_guard lock(mutex);return status;}
bool TakeNotification(Notification& notice){std::lock_guard lock(mutex);if(notices.empty())return false;notice=std::move(notices.front());notices.pop_front();return true;}
LoadScope::LoadScope(D2ClientStrc* client,D2UnitStrc** output){
    if(!installed.load() || !client || !output)return;
    auto next=std::make_unique<State>();
    next->context.client=client;next->context.output=output;
    next->previous=active;state=std::move(next);active=&state->context;
}

namespace {
void Complete(Context& c,bool success,int nativeResult){
    if(c.completed || !c.count || c.blocked)return;
    c.completed=true;
    if(success && c.used && c.readerReturned && c.readerResult==0){
        Report(c.character+": converted "+std::to_string(c.count)+" Candy Corn to quantity 1. Original save backup: "+DisplayPath(c.backup));
        Notice("Candy Corn updated",c.character+": converted "+std::to_string(c.count)+" Candy Corn to stacks of 1. Your original save was backed up.");
    }else{
        std::string reason;
        if(!c.readerReturned)reason="native reader did not return after receiving the converted copy";
        else if(c.readerResult)reason="native reader rejected the converted copy; reader result="+std::to_string(c.readerResult);
        else reason="native reader accepted the converted copy, but character loading did not complete; reader result=0";
        const auto result=nativeResult>=0?std::to_string(nativeResult):std::string("not recorded");
        Report(c.character+": "+reason+"; load result="+result+
            "; Candy Corn="+std::to_string(c.count)+"; original bytes="+std::to_string(c.size)+
            "; converted bytes="+std::to_string(c.converted.size())+
            ". Original save remains unchanged. Backup: "+DisplayPath(c.backup));
        const auto message=c.readerReturned&&c.readerResult?"The game rejected the converted save (code "+std::to_string(c.readerResult)+").":
            "The game could not finish loading the converted character (code "+result+").";
        Notice("Candy Corn conversion could not finish",c.character+": "+message+
            " The original save and its backup are intact. Details: last-conversion.txt.",true);
    }
}
}
void LoadScope::Complete(bool success,int nativeResult){if(state)CandyCornSave::Complete(state->context,success,nativeResult);}
LoadScope::~LoadScope(){if(state){active=state->previous;if(!state->context.completed)try{Complete(false);}catch(...) {}}}
}
