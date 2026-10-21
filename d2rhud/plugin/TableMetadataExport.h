#pragma once
#include "TableMetadata.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <cwctype>

namespace TableMetadataExport {
namespace fs=std::filesystem;
inline bool Enabled(const std::vector<std::wstring>& args){for(auto arg:args){std::transform(arg.begin(),arg.end(),arg.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});if(arg==L"-txt")return true;}return false;}
inline fs::path DataRoot(const fs::path& install,const std::string& mod){
    if(mod.empty())return install/"data";
    if(mod=="."||mod==".."||mod.find_first_of("/\\:")!=mod.npos)throw std::runtime_error("Invalid selected mod name");
    const auto root=install/"Mods"/mod;const auto data=root/(mod+".mpq")/"data";
    return fs::is_directory(data/"global/excel")?data:root/"data";
}
using Rows=std::vector<std::vector<std::string>>;
inline std::string Lower(std::string text){std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});return text;}
inline std::string Read(const fs::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("Cannot read "+path.string());
    const auto size=file.tellg();if(size<0 || size>64*1024*1024)throw std::runtime_error("Invalid table size: "+path.string());
    std::string text(size_t(size),'\0');file.seekg(0);
    if(size && !file.read(text.data(),size))throw std::runtime_error("Incomplete table read: "+path.string());return text;
}
inline Rows Parse(std::string text) {
    if(text.starts_with("\xef\xbb\xbf"))text.erase(0,3);
    std::istringstream input(text);Rows rows;std::string line;
    while(std::getline(input,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::vector<std::string> fields;size_t start=0;
        for(size_t i=0;i<=line.size();++i)if(i==line.size()||line[i]=='\t'){fields.push_back(line.substr(start,i-start));start=i+1;}
        rows.push_back(std::move(fields));
    }
    return rows;
}
inline Rows Records(const Rows& rows){Rows result;for(size_t i=1;i<rows.size();++i)if(!rows[i].empty()&&!rows[i][0].empty()&&Lower(rows[i][0])!="expansion")result.push_back(rows[i]);return result;}
inline uint32_t U32(const std::string& raw,size_t at=0){uint32_t value=0;if(at+4>raw.size())throw std::runtime_error("Truncated BIN");memcpy(&value,raw.data()+at,4);return value;}
inline uint16_t U16(const std::string& raw,size_t at){uint16_t value=0;if(at+2>raw.size())throw std::runtime_error("Truncated BIN");memcpy(&value,raw.data()+at,2);return value;}
inline size_t Column(const std::vector<std::string>& header,const std::string& name){const auto it=std::find(header.begin(),header.end(),name);return size_t(it-header.begin());}
struct Output {nlohmann::json metadata;std::string auxiliary;};
inline Output Build(const fs::path& data) {
    const auto excel=data/"global/excel";Output out;out.metadata={{"version",1},{"tables",nlohmann::json::object()}};
    std::map<std::string,fs::path> sources;
    for(const auto& file:fs::directory_iterator(excel))if(file.is_regular_file()&&Lower(file.path().extension().string())==".txt")sources.emplace(Lower(file.path().stem().string()),file.path());
    for(const auto& [name,source]:sources){
        auto binary=source;binary.replace_extension(".bin");if(!fs::is_regular_file(binary))continue;
        const auto text=Read(source);const auto rows=Parse(text);if(rows.empty())continue;
        auto header=rows[0];for(auto& key:header)key=Lower(key);
        const auto column=Column(header,TableMetadata::NameColumn(name));
        if(column==header.size()&&name!="cubemain"&&name!="monstats"&&name!="superuniques")continue;
        const auto raw=Read(binary);const auto count=U32(raw);
        if(!count || count>1000000 || raw.size()<=4 || (raw.size()-4)%count)throw std::runtime_error("Unsupported or incomplete "+name+".bin");
        const auto records=Records(rows);
        nlohmann::json entry={{"bin_hash",TableMetadata::Hash({reinterpret_cast<const uint8_t*>(raw.data()),raw.size()})},{"bin_size",raw.size()}};
        if(column<header.size()){
            if(records.size()<count)throw std::runtime_error("TXT/BIN row count mismatch: "+name);
            auto& names=entry["names"]=nlohmann::json::array();for(size_t i=0;i<count;++i)names.push_back(column<records[i].size()?records[i][column]:"");
        }
        if(name=="itemtypes"){
            const auto label=Column(header,"itemtype");if(label<header.size()){auto& labels=entry["labels"]=nlohmann::json::array();for(size_t i=0;i<count;++i)labels.push_back(label<records[i].size()?records[i][label]:"");}
        }
        if(name=="cubemain"){
            if(rows.size()-1!=count)throw std::runtime_error("cubemain TXT/BIN row counts differ; compilation is incomplete");
            entry["source"]=text;
        }
        if(name=="monstats"||name=="superuniques"){
            if(records.size()<count)throw std::runtime_error("TXT/BIN row count mismatch: "+name);
            std::vector<size_t> wanted;for(size_t i=0;i<header.size();++i)if(header[i]=="id"||header[i]=="hcidx"||header[i]=="superunique"||header[i].find("treasureclass")!=std::string::npos||header[i].starts_with("tc"))wanted.push_back(i);
            std::string overlay;auto append=[&](const auto& row){bool first=true;for(auto i:wanted){if(!first)overlay+='\t';first=false;if(i<row.size())overlay+=row[i];}overlay+='\n';};
            append(rows[0]);for(size_t i=0;i<count;++i)append(records[i]);entry["overlay"]=std::move(overlay);
        }
        out.metadata["tables"][name]=std::move(entry);
    }
    if(out.metadata["tables"].empty())throw std::runtime_error("No complete TXT/BIN table pairs are available yet");
    if(out.metadata["tables"].contains("treasureclassex")&&sources.contains("monstats")&&fs::is_regular_file(excel/"monstats.bin")){
        const auto tc=Read(excel/"treasureclassex.bin"),mon=Read(excel/"monstats.bin");const auto tcCount=U32(tc),monCount=U32(mon);
        if(tc.size()!=4+size_t(tcCount)*0x2f0 || mon.size()!=4+size_t(monCount)*0x1dc)throw std::runtime_error("Unsupported treasure/monster BIN layout");
        std::map<std::string,int> classes;for(size_t i=0;i<tcCount;++i){const auto name=tc.substr(4+i*0x2f0,32);classes[name.substr(0,name.find('\0'))]=int(i);}
        const auto rows=Parse(Read(sources.at("monstats")));const auto records=Records(rows);if(records.size()<monCount)throw std::runtime_error("Monster TXT/BIN row count mismatch");
        std::set<int> bases;
        for(size_t i=0;i<monCount;++i)for(size_t d=0;d<3;++d)for(size_t v=0;v<3;++v){
            const auto col=Column(rows[0],"TreasureClass"+std::to_string(v+1)+(d==1?"(N)":d==2?"(H)":""));
            if(col>=rows[0].size())throw std::runtime_error("Monster treasure columns are missing");
            const auto name=col<records[i].size()?records[i][col]:"";const auto found=classes.find(name);const auto native=U16(mon,4+i*0x1dc+0xba+d*8+v*2);
            if(found!=classes.end()&&native)bases.insert(int(native)-found->second);
        }
        if(bases.size()!=1 || *bases.begin()<0 || *bases.begin()>65535)throw std::runtime_error("Monster/treasure BINs disagree on native class IDs");
        out.metadata["tables"]["treasureclassex"]["id_base"]=*bases.begin();
    }
    if(const auto it=sources.find("transmog_table");it!=sources.end())out.auxiliary=nlohmann::json{{"version",1},{"source",Read(it->second)}}.dump();
    return out;
}
struct Stamp {uintmax_t size;fs::file_time_type time;bool operator==(const Stamp&)const=default;};
using Snapshot=std::map<fs::path,Stamp>;
inline bool CompiledChanged(const Snapshot& before,const Snapshot& after){
    for(const auto& [name,stamp]:before)if(Lower(name.extension().string())==".bin"){const auto it=after.find(name);if(it==after.end()||it->second!=stamp)return true;}
    for(const auto& [name,stamp]:after)if(Lower(name.extension().string())==".bin"&&!before.contains(name))return true;
    return false;
}
inline Snapshot Capture(const fs::path& data){
    Snapshot result;for(const auto& file:fs::directory_iterator(data/"global/excel")){
        const auto extension=Lower(file.path().extension().string());std::error_code error;
        if((extension!=".txt"&&extension!=".bin")||!file.is_regular_file(error)||error)continue;
        const auto size=file.file_size(error);if(error)continue;const auto time=file.last_write_time(error);if(error)continue;
        result.emplace(file.path().filename(),Stamp{size,time});
    }return result;
}
void Start(const fs::path& install,const std::string& mod);
const char* Status();
}
