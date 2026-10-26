#include "HudLocalization.h"
#include "../resource.h"
#include <windows.h>
#include <fstream>
#include <sstream>

namespace HudLocale {
namespace {
std::once_flag initialized;
std::mutex runtimeMutex;
std::filesystem::path folder,shippedFolder,preferenceFile;
std::string preference="auto",language="enUS",status;
HMODULE module=nullptr;
std::string Resource(int id){
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)return {};
    const auto handle=LoadResource(module,resource);const auto data=LockResource(handle);const auto size=SizeofResource(module,resource);
    return data?std::string(static_cast<const char*>(data),size):std::string{};
}
std::string Read(const std::filesystem::path& path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};
    const auto size=file.tellg();if(size<0 || size>16*1024*1024)throw std::runtime_error("Translation file exceeds 16 MiB.");
    std::string bytes(size_t(size),'\0');file.seekg(0);if(!file.read(bytes.data(),bytes.size()))throw std::runtime_error("Translation file could not be read.");return bytes;
}
std::string Detect(){
    const auto override=CommandLanguage(GetCommandLineW());if(!override.empty())return override;
    wchar_t locale[32]{};DWORD size=sizeof(locale);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Blizzard Entertainment\\Battle.net\\Launch Options\\OSI",L"LOCALE",RRF_RT_REG_SZ,nullptr,locale,&size)==ERROR_SUCCESS){
        std::string code;for(const wchar_t c:std::wstring(locale))if(c<128)code.push_back(char(c));if(Supported(code))return code;
    }return "enUS";
}
bool Load(){
    auto next=std::make_shared<Catalog>();std::string problems;
    int index=0;for(const auto& entry:Languages){if(language==entry.code)break;++index;}
    try{const auto bytes=Resource(IDR_HUD_LANGUAGE_BASE+index);if(!bytes.empty())next->Merge(bytes);}
    catch(const std::exception& error){problems=error.what();}
    for(const auto& directory:{shippedFolder,folder})try{const auto bytes=Read(directory/(language+".json"));if(!bytes.empty())next->Merge(bytes);}
    catch(const std::exception& error){problems+=(problems.empty()?"":" / ")+std::string(error.what());}
    if(next->rejected)problems+=(problems.empty()?"":" / ")+std::to_string(next->rejected)+" invalid entries ignored";
    Publish(next);status=language+": "+std::to_string(next->strings.size())+" translations loaded"+(problems.empty()?".":". "+problems);return problems.empty();
}
}
void Initialize(){std::call_once(initialized,[]{
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&Initialize),&module);
    wchar_t path[32768]{};GetModuleFileNameW(module,path,DWORD(std::size(path)));shippedFolder=std::filesystem::path(path).parent_path()/L"D2RHUD"/L"translations";folder=shippedFolder;
    wchar_t local[32768]{};if(GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)))){preferenceFile=std::filesystem::path(local)/L"D2RHUD"/L"language.json";folder=preferenceFile.parent_path()/L"Translations";}
    try{const auto bytes=Read(preferenceFile);if(!bytes.empty()){const auto config=nlohmann::json::parse(bytes);const auto value=config.value("language",std::string("auto"));if(value=="auto"||Supported(value))preference=value;}}
    catch(...){} // A broken preference cannot block game startup.
    language=preference=="auto"?Detect():preference;Load();
});}
bool Reload(){Initialize();std::lock_guard lock(runtimeMutex);return Load();}
std::string Preference(){Initialize();std::lock_guard lock(runtimeMutex);return preference;}
std::string LanguageCode(){Initialize();return language;}
std::filesystem::path Directory(){Initialize();return folder;}
std::string Status(){Initialize();std::lock_guard lock(runtimeMutex);return status;}
bool PrepareEditor(){
    Initialize();std::lock_guard lock(runtimeMutex);
    try{
        std::filesystem::create_directories(folder);
        const auto write=[&](const std::filesystem::path& path,int id){
            if(std::filesystem::exists(path))return;const auto bytes=Resource(id);if(bytes.empty())throw std::runtime_error("Embedded translation asset is unavailable.");
            std::ofstream file(path,std::ios::binary);if(!file.write(bytes.data(),bytes.size()))throw std::runtime_error("Translation asset could not be exported.");
        };
        for(size_t i=0;i<std::size(Languages);++i)write(folder/(std::string(Languages[i].code)+".json"),IDR_HUD_LANGUAGE_BASE+int(i));
        write(folder/L"editor.html",IDR_HUD_TRANSLATION_EDITOR);write(folder/L"Noto-font-license.txt",IDR_HUD_FONT_LICENSE);return true;
    }catch(const std::exception& error){status=error.what();return false;}
}
bool SetPreference(const std::string& value){
    Initialize();if(value!="auto"&&!Supported(value))return false;std::lock_guard lock(runtimeMutex);
    try{
        if(preferenceFile.empty())throw std::runtime_error("Language preference folder is unavailable.");
        std::filesystem::create_directories(preferenceFile.parent_path());auto temp=preferenceFile;temp+=L".tmp";
        const std::string bytes=nlohmann::json{{"language",value}}.dump(2)+"\n";
        {std::ofstream file(temp,std::ios::binary|std::ios::trunc);if(!file.write(bytes.data(),bytes.size()))throw std::runtime_error("Language preference could not be written.");}
        if(!MoveFileExW(temp.c_str(),preferenceFile.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Language preference could not be saved.");
        preference=value;return true;
    }catch(const std::exception& error){status=error.what();return false;}
}
bool UnicodeFont(){return LanguageCode()!="enUS";}
std::string FontPath(){
    Initialize();wchar_t windows[32768]{};GetWindowsDirectoryW(windows,DWORD(std::size(windows)));
    const auto fonts=std::filesystem::path(windows)/L"Fonts";
    const wchar_t* face=language=="jaJP"?L"meiryo.ttc":language=="koKR"?L"malgun.ttf":language=="zhCN"?L"msyh.ttc":language=="zhTW"?L"msjh.ttc":L"segoeui.ttf";
    const auto path=fonts/face;return std::filesystem::exists(path)?path.string():std::string{};
}
const unsigned char* FontBytes(size_t& size){
    Initialize();const auto resource=FindResourceW(module,MAKEINTRESOURCEW(IDR_HUD_UNICODE_FONT),RT_RCDATA);
    size=resource?SizeofResource(module,resource):0;return resource?static_cast<const unsigned char*>(LockResource(LoadResource(module,resource))):nullptr;
}
std::string GlyphText(){
    Initialize();std::string text;const auto table=active.load();if(table)for(const auto& [key,value]:table->strings){text+=key;text+=value;}return text;
}
}
