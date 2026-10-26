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
// The character-selection preview reads the save before the server loader.
// Its source is a locked byte buffer, not a D2Client save descriptor.
struct PreviewBuffer {SRWLOCK lock{};uint8_t* bytes=nullptr;uint64_t size=0;};
static_assert(sizeof(PreviewBuffer)==24 && offsetof(PreviewBuffer,bytes)==8);
using Preview=bool(__fastcall*)(PreviewBuffer*,void*,void*,void*,uintptr_t);
Preview originalPreview=nullptr;
using StashLoad=int(__fastcall*)(D2GameStrc*,D2ClientStrc*,D2UnitStrc*);
StashLoad originalStashLoad=nullptr;
std::atomic<bool> installed{false};
std::mutex mutex;
std::string status="Candy Corn conversion is not initialized.";
std::filesystem::path directory;
std::deque<Notification> notices;
struct Signature {uint32_t rva,size;uint64_t hash;};
constexpr std::array<Signature,10> signatures{{
    {0x41f570,32,0xf0f8f709a7df0717ULL}, // Locked save-buffer preview; copies bytes before parsing inventory.
    {0x31d540,32,0x9783e7dc39ce527eULL}, // Synchronous current-format reader, no buffer ownership.
    {0x2934ef,32,0x11ea0b69a437d299ULL}, // Caller loads first native descriptor pointer and length.
    {0x31a580,32,0xde2c95873e427480ULL},
    {0x14badb0,511*8,0x3a874274e5683ee4ULL}, // Complete native Huffman code tree.
    {0x1f0290,32,0x685839ffbd0f1ea4ULL},
    {0x1fe680,32,0x279e05c3ab40a8eaULL},
    {0x203c58,32,0xa2b71f88e5ec1ef0ULL},
    {0x319b70,32,0x8bff334cf26eea0bULL}, // Shared-stash loader borrows descriptor entries.
    {0x319e7d,36,0xb30302db8ff58643ULL}, // Item list starts at +64; reader receives descriptor length -64.
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
bool ReadTables(Tables& tables,std::string* error=nullptr){
    auto fail=[&](const char* text){if(error)*error=text;return false;};
    const auto items=Field<const uint8_t*>(Native<void*>(0x1ca0390));
    const auto itemCount=Field<uint32_t>(Native<void*>(0x1ca0398));
    const auto stats=Field<const uint8_t*>(Native<void*>(0x1c9fb78));
    const auto statCount=Field<uint32_t>(Native<void*>(0x1c9fb80));
    const auto types=Field<const uint32_t*>(Native<void*>(0x1c9fc10));
    const auto typeCount=Field<uint32_t>(Native<void*>(0x1c9fbf8));
    const auto typeStride=Field<uint32_t>(Native<void*>(0x1c9fc08));
    if(!items || !stats || !types || !itemCount || itemCount>100000 || !statCount || statCount>4096 ||
        typeCount<=50 || typeCount>4096 || typeStride!=(typeCount+31)/32)return fail("loaded item/stat/type tables are unavailable or have an unsupported layout");
    std::vector<uint8_t> itemCopy(size_t(itemCount)*0x1b4),statCopy(size_t(statCount)*0x144);
    std::vector<uint32_t> typeCopy(size_t(typeCount)*typeStride);
    if(!Read(items,itemCopy.data(),itemCopy.size()) || !Read(stats,statCopy.data(),statCopy.size()) ||
        !Read(types,typeCopy.data(),typeCopy.size()*4))return fail("loaded item/stat/type table memory could not be read");
    if(!DecodeTables(itemCopy,statCopy,typeCopy,typeCount,typeStride,tables))return fail("loaded item/stat/type definitions failed validation");
    return true;
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
    std::array<uint8_t,8> header{};
    if(!Read(source,header.data(),header.size())){c.blocked=true;Report("Candy Corn conversion paused: save header could not be read.");return false;}
    // Older formats still use their native reader and are outside this migration.
    if(Detail::Word(header,0)!=0xaa55aa55 || Detail::Word(header,4)!=98)return false;
    std::vector<uint8_t> bytes(size_t(size),uint8_t(0));
    if(!Read(source,bytes.data(),bytes.size())){c.blocked=true;Report("Candy Corn conversion paused: save bytes could not be read.");return false;}
    std::string error;
    Tables tables;if(!ReadTables(tables,&error)){c.blocked=true;Report("Candy Corn conversion paused: "+error+".");return false;}
    auto result=Convert(bytes,tables);
    if(result.status==CandyCornSaveData::Status::Unchanged)return false;
    if(result.status!=CandyCornSaveData::Status::Converted){c.blocked=true;Report("Save left unchanged; native item parsing was stopped: "+result.message);return false;}
    c.client=client;c.output=output;c.source=source;c.size=size;c.count=result.converted;c.converted=std::move(result.bytes);
    const auto name=std::find(bytes.begin()+0x10b,bytes.begin()+0x148,uint8_t(0));
    c.character=std::string(bytes.begin()+0x10b,name);if(c.character.empty())c.character="Character";
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
bool __fastcall PreviewHook(PreviewBuffer* buffer,void* second,void* third,void* fourth,uintptr_t fifth){
    if(!installed.load() || !buffer)return originalPreview(buffer,second,third,fourth,fifth);
    Context context;
    try {
        std::vector<uint8_t> snapshot;
        {
            struct Lock {SRWLOCK* value;explicit Lock(SRWLOCK* p):value(p){AcquireSRWLockShared(value);}~Lock(){ReleaseSRWLockShared(value);}} lock(&buffer->lock);
            // Mirror the native preview's source layout and lock. Never replace
            // its pointer, length, mutex or allocation, and do not hold the lock
            // while backing up or invoking the native reader.
            if(buffer->size && buffer->size<=8*1024*1024){snapshot.resize(size_t(buffer->size));
                if(!Read(buffer->bytes,snapshot.data(),snapshot.size())){Report("Character preview paused: save buffer could not be read.");return false;}}
        }
        if(snapshot.empty())return originalPreview(buffer,second,third,fourth,fifth);
        const bool ready=Prepare(context,nullptr,snapshot.data(),int32_t(snapshot.size()),nullptr);
        if(context.blocked)return false;
        if(!ready)return originalPreview(buffer,second,third,fourth,fifth);
        PreviewBuffer converted;converted.bytes=context.converted.data();converted.size=context.converted.size();
        const bool accepted=originalPreview(&converted,second,third,fourth,fifth);
        Report(context.character+(accepted?": prepared ":": native preview rejected ")+std::to_string(context.count)+
            " converted Candy Corn for character selection. Original save backup: "+DisplayPath(context.backup));
        if(!accepted)Notice("Candy Corn conversion could not finish",context.character+": character preview could not be loaded. The original save and backup are intact.",true);
        return accepted;
    }catch(const std::exception& e){Report(std::string("Character preview conversion paused: ")+e.what());return false;}
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
        context->blocked=true;
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
struct StashDescriptor {uint8_t* bytes;uint64_t size,capacity;};
static_assert(sizeof(StashDescriptor)==24);
int __fastcall StashLoadHook(D2GameStrc* game,D2ClientStrc* client,D2UnitStrc* player){
    if(!installed.load() || !client)return originalStashLoad(game,client,player);
    struct ConvertedTab {size_t index;std::vector<uint8_t> bytes;};
    std::vector<StashDescriptor> shadow;
    std::vector<ConvertedTab> tabs;
    std::vector<uint8_t> originalPage;
    std::filesystem::path backup;
    size_t total=0;bool identified=false;
    try {
        const auto count=client->nSaveHeaderSize;
        if(!client->pSaveHeaders || !count || count>1025)return originalStashLoad(game,client,player);
        shadow.resize(size_t(count));
        if(!Read(client->pSaveHeaders,shadow.data(),shadow.size()*sizeof(StashDescriptor)))return originalStashLoad(game,client,player);
        Tables tables;std::string tableError;
        if(!ReadTables(tables,&tableError)){Report("Shared stash conversion paused: "+tableError+".");return 27;}
        // Descriptor zero is normally the character. Recognize stash headers
        // rather than relying on an index, filename, page number or SC/HC mode.
        for(size_t index=0;index<shadow.size();++index){
            const auto& entry=shadow[index];std::array<uint8_t,12> header{};
            if(!entry.bytes || entry.size<68 || !Read(entry.bytes,header.data(),header.size()) ||
                Detail::Word(header,0)!=0xaa55aa55 || Detail::Word(header,4)>1)continue;
            if(entry.size>8*1024*1024-originalPage.size()){Report("Shared stash conversion refused: attached stash exceeds backup size limits.");return 27;}
            std::vector<uint8_t> bytes(size_t(entry.size),uint8_t(0));
            if(!Read(entry.bytes,bytes.data(),bytes.size())){Report("Shared stash conversion refused: an original tab could not be read for backup.");return 27;}
            originalPage.insert(originalPage.end(),bytes.begin(),bytes.end());
            if(Detail::Word(header,8)!=98)continue;
            auto result=ConvertStash(bytes,tables);
            if(result.status==CandyCornSaveData::Status::Unchanged)continue;
            if(result.status!=CandyCornSaveData::Status::Converted){Report("Shared stash left unchanged; native item parsing was stopped: "+result.message);return 27;}
            identified=true;total+=result.converted;
            tabs.push_back({index,std::move(result.bytes)});
        }
        if(tabs.empty())return originalStashLoad(game,client,player);
        // Reassemble the original .d2i page in descriptor order, including
        // unchanged tabs and gold. Back up the entire page before conversion.
        std::string error;
        if(!Storage::Backup(directory,originalPage,backup,error,true)){
            Report("Shared stash Candy Corn conversion paused: original page could not be backed up: "+error);
            Notice("Candy Corn conversion paused","The original shared stash could not be backed up. Free disk space or check folder access, then try loading again.",true);
            return 27; // Native invalid-stash result, before creating any stash units.
        }
        for(auto& tab:tabs){
            shadow[tab.index].bytes=tab.bytes.data();
            shadow[tab.index].size=shadow[tab.index].capacity=tab.bytes.size();
        }
    }catch(const std::exception& e){
        Report(std::string("Shared stash Candy Corn conversion could not prepare the tabs: ")+e.what());
        if(identified)return 27;
        return originalStashLoad(game,client,player);
    }
    Notice("Updating Candy Corn","Your shared stash Candy Corn are being converted for the new update.");
    Report("Shared stash: supplying "+std::to_string(tabs.size())+" converted tabs to the native reader; Candy Corn="+std::to_string(total)+
        ". Original page backup: "+DisplayPath(backup));
    // This native routine only borrows the vector and its buffers. It does not
    // free them. Restore the original descriptor pointer before its caller's
    // cleanup; no native allocation is replaced, resized or freed by the HUD.
    struct Borrow {
        D2ClientStrc* client;D2SaveHeadersStrc* original;
        ~Borrow(){client->pSaveHeaders=original;}
    };
    int result;
    {
        Borrow borrow{client,client->pSaveHeaders};
        client->pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(shadow.data());
        result=originalStashLoad(game,client,player);
    }
    if(!result){
        Report("Shared stash: converted "+std::to_string(total)+" Candy Corn to quantity 1 across "+std::to_string(tabs.size())+" tabs. Original page backup: "+DisplayPath(backup));
        Notice("Candy Corn updated","Shared stash: converted "+std::to_string(total)+" Candy Corn to stacks of 1. Your original page was backed up.");
    }else {
        Report("Shared stash: native reader rejected the converted tabs; load result="+std::to_string(result)+". Original page backup: "+DisplayPath(backup));
        Notice("Candy Corn conversion could not finish","The game could not load the converted shared stash. The original page was backed up. Details: last-conversion.txt.",true);
    }
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
    const auto stashTarget=Native<void*>(0x319b70);
    if(MH_CreateHook(stashTarget,reinterpret_cast<void*>(StashLoadHook),reinterpret_cast<void**>(&originalStashLoad))!=MH_OK){
        MH_RemoveHook(target);originalLoad=nullptr;Report("Candy Corn conversion shared-stash hook is unavailable.");return;
    }
    const auto previewTarget=Native<void*>(0x41f570);
    if(MH_CreateHook(previewTarget,reinterpret_cast<void*>(PreviewHook),reinterpret_cast<void**>(&originalPreview))!=MH_OK){
        MH_RemoveHook(stashTarget);MH_RemoveHook(target);originalLoad=nullptr;originalStashLoad=nullptr;Report("Candy Corn conversion preview hook is unavailable.");return;
    }
    bool enabled=true;for(auto hook:{target,stashTarget,previewTarget})if(MH_QueueEnableHook(hook)!=MH_OK)enabled=false;
    enabled=enabled && MH_ApplyQueued()==MH_OK;
    if(!enabled){for(auto hook:{target,stashTarget,previewTarget}){MH_DisableHook(hook);MH_RemoveHook(hook);}originalLoad=nullptr;originalStashLoad=nullptr;originalPreview=nullptr;Report("Candy Corn conversion hooks could not be enabled.");return;}
    installed.store(true);Report("Automatic Candy Corn conversion is ready for character previews, game loading and shared stashes; originals are backed up before conversion.");
}
void Uninstall(){if(installed.exchange(false))for(auto rva:{0x31d540u,0x319b70u,0x41f570u}){const auto target=Native<void*>(rva);MH_DisableHook(target);MH_RemoveHook(target);}originalLoad=nullptr;originalStashLoad=nullptr;originalPreview=nullptr;}
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
