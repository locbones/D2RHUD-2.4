#include "HudConfigStore.h"
#include "../../ProcessLifetime.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <mutex>
#include <sstream>
#include <fstream>
#include <cwctype>

namespace HudConfig {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Temporary {
    std::filesystem::path path;
    ~Temporary() { if (!path.empty()) DeleteFileW(path.c_str()); }
};
std::mutex& StoreMutex() { return HudLifetime::State<std::mutex>(); }
Result Failure(const char* operation, DWORD error = GetLastError()) {
    return {false, std::string(operation) + " (Windows error " + std::to_string(error) + ")"};
}
Result ReadBytes(const std::filesystem::path& path, std::string& bytes, bool& missing) {
    Handle file{CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr)};
    missing = false;
    if (file.value == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        missing = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        return Failure("Could not read HUD preferences", error);
    }
    bytes.clear();
    char buffer[8192];
    DWORD count = 0;
    do {
        if (!ReadFile(file.value, buffer, sizeof(buffer), &count, nullptr))
            return Failure("Could not read HUD preferences");
        bytes.append(buffer, count);
    } while (count);
    return {true};
}
Result Replace(const std::filesystem::path& target, const std::string& bytes,
               bool existed, const std::string& expected) {
    static std::atomic<uint64_t> serial{0};
    Temporary temporary;
    Handle file;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary.path = target;
        temporary.path += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(++serial);
        file.value = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file.value != INVALID_HANDLE_VALUE) break;
        const auto error = GetLastError();
        // A colliding file belongs to someone else; never remove it.
        temporary.path.clear();
        if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
            return Failure("Could not create temporary HUD preferences", error);
    }
    if (file.value == INVALID_HANDLE_VALUE)
        return {false, "Could not reserve a temporary HUD preferences file"};
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const auto count = DWORD((std::min)(bytes.size() - offset, size_t(MAXDWORD)));
        if (!WriteFile(file.value, bytes.data() + offset, count, &written, nullptr))
            return Failure("Could not write temporary HUD preferences");
        if (!written) return {false, "Writing temporary HUD preferences made no progress"};
        offset += written;
    }
    if (!FlushFileBuffers(file.value)) return Failure("Could not flush temporary HUD preferences");
    const auto handle = file.value;
    file.value = INVALID_HANDLE_VALUE;
    if (!CloseHandle(handle)) return Failure("Could not close temporary HUD preferences");

    // Detect external edits made while preparing the replacement.
    std::string current;
    bool missing = false;
    const auto read = ReadBytes(target, current, missing);
    if ((existed && (!read || current != expected)) || (!existed && !missing))
        return {false, "HUD preferences changed on disk; retry saving after reloading them"};
    const bool replaced = existed
        ? ReplaceFileW(target.c_str(), temporary.path.c_str(), nullptr, 0, nullptr, nullptr) != FALSE
        : MoveFileExW(temporary.path.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!replaced) return Failure("Could not replace HUD preferences");
    temporary.path.clear();
    return {true, {}, true};
}
}

Result Parse(std::string_view source, Json& document) {
    try {
        std::string clean(source);
        bool quoted = false;
        for (size_t i = 0; i < clean.size(); ++i) {
            if (quoted) {
                if (clean[i] == '\\') { if (i + 1 < clean.size()) ++i; }
                else if (clean[i] == '"') quoted = false;
                continue;
            }
            if (clean[i] == '"') { quoted = true; continue; }
            if (clean[i] != '/' || i + 1 >= clean.size()) continue;
            if (clean[i + 1] == '/') {
                clean[i++] = ' '; clean[i] = ' ';
                while (i + 1 < clean.size() && clean[i + 1] != '\n') clean[++i] = ' ';
            } else if (clean[i + 1] == '*') {
                clean[i++] = ' '; clean[i] = ' ';
                bool closed = false;
                while (++i < clean.size()) {
                    if (clean[i] == '*' && i + 1 < clean.size() && clean[i + 1] == '/') {
                        clean[i++] = ' '; clean[i] = ' '; closed = true; break;
                    }
                    if (clean[i] != '\n' && clean[i] != '\r') clean[i] = ' ';
                }
                if (!closed) return {false, "HUD preferences contain an unterminated comment"};
            }
        }
        quoted = false;
        for (size_t i = 0; i < clean.size(); ++i) {
            if (quoted) {
                if (clean[i] == '\\') { if (i + 1 < clean.size()) ++i; }
                else if (clean[i] == '"') quoted = false;
            } else if (clean[i] == '"') quoted = true;
            else if (clean[i] == ',') {
                size_t next = i + 1;
                while (next < clean.size() && std::isspace(static_cast<unsigned char>(clean[next]))) ++next;
                size_t previous = i;
                while (previous && std::isspace(static_cast<unsigned char>(clean[previous - 1]))) --previous;
                if (previous && clean[previous - 1] != '{' && clean[previous - 1] != '[' &&
                    next < clean.size() && (clean[next] == ']' || clean[next] == '}')) clean[i] = ' ';
            }
        }
        auto parsed = Json::parse(clean);
        if (!parsed.is_object()) return {false, "HUD preferences must contain a JSON object"};
        document = std::move(parsed);
        return {true};
    } catch (const std::exception& error) { return {false, error.what()}; }
}

Result Read(const std::filesystem::path& path, Json& document) {
    std::lock_guard lock(StoreMutex());
    std::string bytes;
    bool missing = false;
    const auto read = ReadBytes(path, bytes, missing);
    return read ? Parse(bytes, document) : read;
}

std::string Serialize(const Json& document) {
    if (!document.is_object()) throw std::invalid_argument("HUD preferences must contain a JSON object");
    std::ostringstream text;
    text << "{\n";
    bool first = true;
    for (const auto& [key, value] : document.items()) {
        if (!first) text << ",\n";
        first = false;
        text << "    " << Json(key).dump() << ": ";
        // Keep hotkeys and custom commands easy to edit, one entry per line.
        if ((key == "Keybinds" || key == "Commands") && value.is_object()) {
            text << "{\n";
            bool firstEntry = true;
            for (const auto& [name, entry] : value.items()) {
                if (!firstEntry) text << ",\n";
                firstEntry = false;
                text << "        " << Json(name).dump() << ": ";
                if (name == "Custom Commands" && entry.is_array()) {
                    text << "[";
                    for (size_t i = 0; i < entry.size(); ++i)
                        text << (i ? ",\n            " : "\n            ") << entry[i].dump();
                    if (!entry.empty()) text << "\n        ";
                    text << "]";
                } else text << entry.dump();
            }
            text << "\n    }";
        } else text << value.dump(4);
    }
    text << "\n}\n";
    return text.str();
}

Result Update(const std::filesystem::path& path, const std::function<void(Json&)>& edit) {
    std::lock_guard lock(StoreMutex());
    try {
        // No persistent .lock sidecars. Serialize writers across game processes
        // as well as threads, while unrelated feature sections share a file.
        uint64_t key=14695981039346656037ULL;
        for(auto c:std::filesystem::absolute(path).lexically_normal().wstring())key=(key^uint16_t(std::towlower(c)))*1099511628211ULL;
        const auto name=L"Local\\D2RHUD-Preferences-"+std::to_wstring(key);
        const auto lease=CreateMutexW(nullptr,FALSE,name.c_str());
        if(!lease)return Failure("Could not open HUD preferences writer lock");
        struct Lease {HANDLE h;bool owned=false;~Lease(){if(owned)ReleaseMutex(h);CloseHandle(h);}} writer{lease};
        const auto wait=WaitForSingleObject(lease,0);
        writer.owned=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
        if(!writer.owned)return {false,"Another game instance is saving HUD preferences; retry shortly"};
        std::string bytes;
        bool missing = false;
        const auto read = ReadBytes(path, bytes, missing);
        if (!read && !missing) return read;
        Json existing = Json::object();
        if (read) {
            const auto parsed = Parse(bytes, existing);
            if (!parsed) return parsed;
        }
        Json candidate = existing;
        edit(candidate);
        const auto serialized = Serialize(candidate);
        if (!missing && candidate == existing) return {true};
        const auto parent = path.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        return Replace(path, serialized, !missing, bytes);
    } catch (const std::exception& error) { return {false, error.what()}; }
}
}

namespace HudUserConfig {
namespace {
using Json=HudConfig::Json;using Result=HudConfig::Result;
struct Legacy {const char* section;const wchar_t* relative;bool text=false;};
constexpr Legacy legacy[]{
    {"RunTracker",L"RunTracker/settings.json"},{"Language",L"language.json"},
    {"Diagnostics",L"diagnostics.json"},{"Controls",L"controls.json"},{"WindowLayout",L"ui-layout.ini",true}
};
void Envelope(Json& document){
    if(document.empty())document={{"schema",1},{"sections",Json::object()}};
    if(!document.is_object()||document.value("schema",0)!=1||!document.contains("sections")||!document["sections"].is_object())throw std::runtime_error("Unsupported shared HUD preferences; existing file kept.");
}
Result Document(const std::filesystem::path& path,Json& document){
    try{if(!std::filesystem::exists(path)){document=Json::object();Envelope(document);return {true};}
        auto result=HudConfig::Read(path,document);if(!result)return result;Envelope(document);return {true};
    }catch(const std::exception& e){return {false,e.what()};}
}
std::string Bytes(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);const auto size=input.tellg();
    if(size<0||size>4*1024*1024)throw std::runtime_error("Legacy HUD preference is unreadable or exceeds 4 MiB.");
    std::string bytes(size_t(size),'\0');input.seekg(0);if(!bytes.empty()&&!input.read(bytes.data(),size))throw std::runtime_error("Legacy HUD preference could not be read.");return bytes;
}
struct Import {Legacy source;std::filesystem::path path;Json value;std::string bytes;};
void RemoveArchived(const std::filesystem::path& path,const std::string& expected){
    // Deny writes/replacement between checking the archived bytes and deleting
    // this particular file. An older running HUD may still own a legacy copy.
    const auto handle=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return;
    struct Close {HANDLE h;~Close(){CloseHandle(h);}} close{handle};
    LARGE_INTEGER size{};if(!GetFileSizeEx(handle,&size)||size.QuadPart!=expected.size())return;
    std::string bytes(expected.size(),'\0');size_t at=0;
    while(at<bytes.size()){DWORD count=0;if(!ReadFile(handle,bytes.data()+at,DWORD(bytes.size()-at),&count,nullptr)||!count)return;at+=count;}
    if(bytes!=expected)return;
    FILE_DISPOSITION_INFO disposition{TRUE};SetFileInformationByHandle(handle,FileDispositionInfo,&disposition,sizeof(disposition));
}
Result Migrate(const std::filesystem::path& directory){
    try{
        if(directory.empty())return {false,"User preferences folder is unavailable."};
        const auto target=Path(directory);Json current;const auto read=Document(target,current);if(!read)return read;
        std::vector<Import> imports;
        for(const auto& entry:legacy){const auto path=directory/entry.relative;
            try{if(!std::filesystem::exists(path))continue;auto bytes=Bytes(path);Json value;
                if(entry.text)value={{"ini",bytes}};
                else if(!HudConfig::Parse(bytes,value))continue;
                (void)Json(bytes).dump(); // Archive only valid UTF-8; isolate a damaged source.
                imports.push_back({entry,path,std::move(value),std::move(bytes)});
            }catch(...){/* Leave a damaged legacy file for its own feature to report. */}
        }
        if(imports.empty())return {true};
        const auto backup=directory/L"preferences-migration-backup.json";
        const auto archived=HudConfig::Update(backup,[&](Json& j){
            if(j.empty())j={{"schema",1},{"legacy",Json::object()}};
            if(j.value("schema",0)!=1||!j["legacy"].is_object())throw std::runtime_error("Preference migration backup is invalid; legacy files kept.");
            for(const auto& item:imports)if(!j["legacy"].contains(item.source.section))j["legacy"][item.source.section]={{"path",std::filesystem::path(item.source.relative).generic_string()},{"bytes",item.bytes}};
        });if(!archived)return archived;
        const auto saved=HudConfig::Update(target,[&](Json& j){Envelope(j);for(const auto& item:imports)if(!j["sections"].contains(item.source.section))j["sections"][item.source.section]=item.value;});if(!saved)return saved;
        Json verified,archive;if(!Document(target,verified)||!HudConfig::Read(backup,archive))return {false,"Preference migration verification failed; legacy files kept."};
        // A shared section always wins over an older legacy copy. Exact original
        // bytes must be in the single recovery archive before removing its file.
        for(const auto& item:imports)try{
            if(verified["sections"].contains(item.source.section)&&verified["sections"][item.source.section].is_object()&&archive.at("legacy").at(item.source.section).at("bytes")==item.bytes)RemoveArchived(item.path,item.bytes);
        }catch(...){/* Failure to tidy a legacy copy cannot discard settings. */}
        return {true};
    }catch(const std::exception& e){return {false,e.what()};}
}
}
std::filesystem::path Directory(){wchar_t local[32768]{};const auto size=GetEnvironmentVariableW(L"LOCALAPPDATA",local,DWORD(std::size(local)));return size&&size<std::size(local)?std::filesystem::path(local)/L"D2RHUD":std::filesystem::path{};}
std::filesystem::path Path(const std::filesystem::path& directory){const auto root=directory.empty()?Directory():directory;return root.empty()?std::filesystem::path{}:root/L"preferences.json";}
HudConfig::Result Read(const char* section,HudConfig::Json& value,const std::filesystem::path& directory){
    const auto root=directory.empty()?Directory():directory;value=Json::object();
    const auto migrated=Migrate(root);
    Json document;const auto loaded=Document(Path(root),document);
    if(loaded){const auto at=document["sections"].find(section);
        if(at!=document["sections"].end()){if(!at->is_object())return {false,"HUD preference section is invalid; existing file kept."};value=*at;return {true};}
    }
    if(!migrated){
        // A blocked migration must not reset working hotkeys, language or timer
        // preferences. Read the intact old source, but keep edits read-only until
        // it can be archived and migrated successfully.
        for(const auto& entry:legacy)if(std::string_view(section)==entry.section)try{
            const auto bytes=Bytes(root/entry.relative);(void)Json(bytes).dump();
            if(entry.text)value={{"ini",bytes}};else if(!HudConfig::Parse(bytes,value))return migrated;
            return {true,"Preference migration is deferred; original settings remain active. "+migrated.error};
        }catch(...){}
        return migrated;
    }
    if(!loaded)return loaded;
    for(const auto& entry:legacy)if(std::string_view(section)==entry.section)try{if(std::filesystem::exists(root/entry.relative))return {false,"Legacy HUD preferences could not be migrated; original file kept: "+(root/entry.relative).string()};}catch(const std::exception& e){return {false,e.what()};}
    return {true};
}
HudConfig::Result Update(const char* section,const std::function<void(HudConfig::Json&)>& edit,const std::filesystem::path& directory){
    const auto root=directory.empty()?Directory():directory;Json previous;const auto read=Read(section,previous,root);if(!read||!read.error.empty())return {false,read.error};
    return HudConfig::Update(Path(root),[&](Json& document){Envelope(document);auto& value=document["sections"][section];if(value.is_null())value=Json::object();if(!value.is_object())throw std::runtime_error("HUD preference section is invalid; existing file kept.");edit(value);});
}
}
