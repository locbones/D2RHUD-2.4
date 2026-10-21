#include "TableMetadataExport.h"
#include <Windows.h>
#include <shellapi.h>
#include <atomic>
#include <mutex>
#include <MinHook.h>

namespace TableMetadataExport {
namespace {
std::atomic<const char*> status{"Automatic metadata export is inactive (no -txt)."};
std::atomic<bool> started{false};
std::atomic<uint64_t> serial{0};
std::mutex exportMutex;
fs::path completionData;
using Completion=void(__fastcall*)();
Completion originalComplete=nullptr;
struct PublishError:std::runtime_error{using std::runtime_error::runtime_error;};
struct Pending {
    fs::path target,temp;
    bool changed=false;
    Pending(const fs::path& path,const std::string& text):target(path){
        try{if(fs::is_regular_file(path)&&Read(path)==text)return;}catch(...){}
        std::error_code directoryError;fs::create_directories(path.parent_path(),directoryError);
        if(directoryError)throw PublishError("Could not create "+path.parent_path().string());temp=path;
        temp+=L".pending-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);output.write(text.data(),text.size());output.close();
        if(!output){std::error_code ignored;fs::remove(temp,ignored);throw PublishError("Could not stage "+path.string());}
        changed=true;
    }
    ~Pending(){if(changed){std::error_code ignored;fs::remove(temp,ignored);}}
    void Commit(){if(changed){if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw PublishError("Could not replace "+target.string()+" (Windows error "+std::to_string(GetLastError())+")");changed=false;}}
};
void Log(const fs::path& data,const std::string& message){
    OutputDebugStringA(("D2RHUD table metadata: "+message+"\n").c_str());
    try{
        wchar_t local[32768]{};const auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);if(!size||size>=32768)return;
        const auto folder=fs::path(local)/"D2RHUD";fs::create_directories(folder);
        std::ofstream file(folder/"table-metadata-export.log",std::ios::binary|std::ios::trunc);
        file<<message<<"\nData: "<<data.string()<<"\n";
    }catch(...){}
}
bool Generate(const fs::path& data){
    std::lock_guard lock(exportMutex);
    const auto before=Capture(data);const auto output=Build(data);
    Pending metadata(data/"D2RHUD/table-metadata.json",output.metadata.dump());
    std::unique_ptr<Pending> auxiliary;
    if(!output.auxiliary.empty())auxiliary=std::make_unique<Pending>(data/"D2RHUD/transmog_table.json",output.auxiliary);
    if(before!=Capture(data))return false;
    if(auxiliary)auxiliary->Commit();metadata.Commit();
    status="Automatic metadata export is synchronized with the compiled tables.";
    Log(data,"Exported "+std::to_string(output.metadata["tables"].size())+" checksum-bound table companions after -txt compilation.");return true;
}
void __fastcall Complete(){
    originalComplete();
    // DifficultyLevels is the final table loader in the supported startup
    // sequence (after CubeMain). Finish before control can reach a failing
    // UI/asset load in short -direct compilation runs.
    try{if(!Generate(completionData))status="Compiler output changed during export; background retry is pending.";}
    catch(const std::exception& error){status="Automatic metadata export deferred; background retry is pending.";Log(completionData,error.what());}
}
bool SupportedCompletion(uintptr_t image){
    if(!image)return false;
    for(const auto& [rva,size,hash]:std::array<std::tuple<uint32_t,size_t,const char*>,2>{{
        {0x1dac20,42,"70675ff591568f0f"},{0x1dfab7,17,"2637192ee6c80adc"}}}){
        std::array<uint8_t,42> bytes{};SIZE_T got=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(image+rva),bytes.data(),size,&got)||got!=size||TableMetadata::Hash({bytes.data(),size})!=hash)return false;
    }return true;
}
bool InstallCompletion(uintptr_t image){
    if(!SupportedCompletion(image))return false;
    const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    const auto target=reinterpret_cast<void*>(image+0x1dac20);
    if(MH_CreateHook(target,reinterpret_cast<void*>(Complete),reinterpret_cast<void**>(&originalComplete))!=MH_OK)return false;
    if(MH_EnableHook(target)==MH_OK)return true;
    MH_RemoveHook(target);return false;
}
struct Watch {fs::path data;};
DWORD WINAPI Run(void* parameter){
    std::unique_ptr<Watch> context(static_cast<Watch*>(parameter));
    const auto change=FindFirstChangeNotificationW((context->data/"global/excel").c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE);
    if(change==INVALID_HANDLE_VALUE){status="Automatic metadata export could not watch the compiler output.";Log(context->data,Status());return 0;}
    struct Close {HANDLE handle;~Close(){FindCloseChangeNotification(handle);}} close{change};
    try{
        auto snapshot=Capture(context->data);bool dirty=true;auto quietSince=GetTickCount64();DWORD settle=1500;
        for(;;){
            const auto wait=WaitForSingleObject(change,dirty?250:INFINITE);
            if(wait==WAIT_OBJECT_0){
                if(!FindNextChangeNotification(change))throw std::runtime_error("Directory notification could not be rearmed");
                const auto current=Capture(context->data);
                if(current!=snapshot){
                    // Editing TXT alone does not mean the game compiled it.
                    // Wait for BIN activity before refreshing an idle export.
                    dirty=dirty||CompiledChanged(snapshot,current);snapshot=current;
                    if(dirty){quietSince=GetTickCount64();settle=1500;status="Waiting for table compilation writes to finish.";}
                }
            }else if(wait!=WAIT_TIMEOUT)throw std::runtime_error("Directory notification failed");
            if(!dirty||GetTickCount64()-quietSince<settle)continue;
            try{
                if(!Generate(context->data)){snapshot=Capture(context->data);quietSince=GetTickCount64();continue;}
                dirty=false;snapshot=Capture(context->data);
            }catch(const PublishError& error){
                snapshot=Capture(context->data);dirty=true;quietSince=GetTickCount64();settle=5000;
                status="Automatic metadata export will retry a locked/unwritable output; see table-metadata-export.log.";
                Log(context->data,error.what());
            }catch(const std::exception& error){
                const auto current=Capture(context->data);
                dirty=current!=snapshot;snapshot=current;quietSince=GetTickCount64();
                status="Automatic metadata export deferred; see table-metadata-export.log.";
                Log(context->data,std::string("Export deferred; previous JSON companions retained: ")+error.what());
            }
        }
    }catch(const std::exception& error){status="Automatic metadata export stopped; see table-metadata-export.log.";Log(context->data,error.what());}
    return 0;
}
}
const char* Status(){return status.load();}
void Start(const fs::path& install,const std::string& mod){
    int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);if(!argv)return;
    std::vector<std::wstring> args;for(int i=1;i<count;++i)args.emplace_back(argv[i]);LocalFree(argv);
    if(!Enabled(args)||started.exchange(true))return;
    try{
        auto context=std::make_unique<Watch>();context->data=DataRoot(install,mod);
        if(!fs::is_directory(context->data/"global/excel"))throw std::runtime_error("Compiler table folder does not exist");
        // This authoring-only worker lives with the process; avoid unloading
        // its code while it owns an asynchronous directory notification.
        HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Start),&pinned))throw std::runtime_error("Could not retain the export worker module");
        completionData=context->data;
        const bool completion=InstallCompletion(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
        const auto worker=CreateThread(nullptr,0,Run,context.get(),0,nullptr);
        if(!worker)throw std::runtime_error("Could not start the export worker");
        context.release();CloseHandle(worker);status=completion?"Automatic metadata export enabled at native table completion.":"Automatic metadata export enabled through directory notifications (native completion signature unavailable).";
    }catch(const std::exception& error){status="Automatic metadata export could not start; see table-metadata-export.log.";Log(install/"data",error.what());}
}
}
