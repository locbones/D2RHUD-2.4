#pragma once
#include "../d2/json.hpp"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>

namespace HudLocale {
struct Language {const char* code;const char* name;};
inline constexpr Language Languages[]{
    {"enUS","English"},{"deDE","German"},{"esES","Spanish (Spain)"},{"esMX","Spanish (Latin America)"},
    {"frFR","French"},{"itIT","Italian"},{"jaJP","Japanese"},{"koKR","Korean"},{"plPL","Polish"},
    {"ptBR","Portuguese (Brazil)"},{"ruRU","Russian"},{"zhCN","Chinese (Simplified)"},{"zhTW","Chinese (Traditional)"}
};
inline bool Supported(std::string_view code){for(const auto& language:Languages)if(code==language.code)return true;return false;}
inline std::string CommandLanguage(const std::wstring& command){
    static const std::wregex pattern(LR"locale((?:^|\s)-locale(?:=|\s+)"?([A-Za-z]{4})"?(?=\s|$))locale",std::regex::icase);
    std::wsmatch argument;if(!std::regex_search(command,argument,pattern))return {};
    std::string code;for(const wchar_t c:argument[1].str())code.push_back(char(c));
    for(size_t i=0;i<code.size();++i)code[i]=char(i<2?std::tolower(static_cast<unsigned char>(code[i])):std::toupper(static_cast<unsigned char>(code[i])));
    return Supported(code)?code:std::string{};
}
// Case-insensitive search for the scripts used by the supported UI languages.
inline std::string Fold(std::string_view text){
    std::string result;
    for(size_t i=0;i<text.size();){
        const auto first=static_cast<unsigned char>(text[i++]);unsigned cp=first;
        int count=first<0x80?0:first<0xe0?1:first<0xf0?2:3;
        if(count){cp=first&((1u<<(6-count))-1);while(count--&&i<text.size())cp=(cp<<6)|(static_cast<unsigned char>(text[i++])&63);}
        if((cp>='A'&&cp<='Z')||(cp>=0xc0&&cp<=0xde&&cp!=0xd7)||(cp>=0x410&&cp<=0x42f))cp+=32;
        else if(cp>=0x400&&cp<=0x40f)cp+=80;
        else if((cp>=0x100&&cp<=0x12f&&!(cp&1))||(cp>=0x132&&cp<=0x137&&!(cp&1))||
                (cp>=0x139&&cp<=0x148&&(cp&1))||(cp>=0x14a&&cp<=0x177&&!(cp&1))||(cp>=0x179&&cp<=0x17e&&(cp&1)))++cp;
        else if(cp==0x178)cp=0xff;
        else if(cp==0x1e9e)cp=0xdf;
        if(cp<0x80)result+=char(cp);
        else if(cp<0x800){result+=char(0xc0|(cp>>6));result+=char(0x80|(cp&63));}
        else if(cp<0x10000){result+=char(0xe0|(cp>>12));result+=char(0x80|((cp>>6)&63));result+=char(0x80|(cp&63));}
        else {result+=char(0xf0|(cp>>18));result+=char(0x80|((cp>>12)&63));result+=char(0x80|((cp>>6)&63));result+=char(0x80|(cp&63));}
    }return result;
}
inline std::vector<std::string> Placeholders(const std::string& text){
    static const std::regex pattern(R"(%[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|I64|I32|[hljztL])?[diuoxXfFeEgGaAcsnp%])");
    std::vector<std::string> result;for(auto i=std::sregex_iterator(text.begin(),text.end(),pattern);i!=std::sregex_iterator();++i)result.push_back(i->str());return result;
}
struct Catalog {
    std::map<std::string,std::string,std::less<>> strings;
    size_t rejected=0;
    void Merge(const std::string& bytes){
        const auto root=nlohmann::json::parse(bytes);
        if(!root.is_object() || !root.contains("strings") || !root["strings"].is_object())throw std::runtime_error("Translation file must contain a strings object.");
        for(const auto& [key,value]:root["strings"].items()){
            if(!value.is_string()){++rejected;continue;}
            const auto text=value.get<std::string>();
            if(text.empty() || text.find('\0')!=text.npos || text.find("##")!=text.npos ||
               std::count(key.begin(),key.end(),'%')!=std::count(text.begin(),text.end(),'%') || Placeholders(key)!=Placeholders(text)){++rejected;continue;}
            strings[key]=text;
        }
    }
};
inline std::atomic<std::shared_ptr<const Catalog>> active;
inline std::mutex publicationMutex;
inline std::vector<std::shared_ptr<const Catalog>> retained;
inline void Publish(std::shared_ptr<const Catalog> next){
    // Retain old catalogs so widget labels/arguments remain valid during a reload.
    std::lock_guard lock(publicationMutex);retained.push_back(next);active.store(std::move(next));
}
inline const char* Tr(const char* text){
    if(!text)return nullptr;const auto table=active.load();if(!table)return text;
    const auto found=table->strings.find(text);return found==table->strings.end()?text:found->second.c_str();
}
inline std::string Translate(std::string_view text){const std::string key(text);return Tr(key.c_str());}
inline std::string Visible(std::string_view text){return Translate(text.substr(0,text.find("##")));}
inline std::string Label(const char* source){
    if(!source)return {};const std::string text(source);const auto split=text.find("##");
    if(split==0)return text;const auto english=text.substr(0,split),translated=Translate(english);
    const auto reset=text.find("###");return translated+"###"+(reset==text.npos?text:text.substr(reset+3));
}
// Startup and explicit reload own file I/O. Rendering only reads the catalog.
void Initialize();
bool Reload();
std::string Preference();
std::string LanguageCode();
bool SetPreference(const std::string&);
std::filesystem::path Directory();
std::string Status();
bool PrepareEditor();
bool UnicodeFont();
std::string FontPath();
const unsigned char* FontBytes(size_t&);
std::string GlyphText();
}
