#define NOMINMAX
#include "../d2rhud/plugin/TableMetadataExport.h"
#include <Windows.h>
#include <cassert>
#include <iostream>

using namespace TableMetadataExport;
void Write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(text.data(),text.size());file.close();assert(file);}
bool Wait(const fs::path& file,const nlohmann::json& expected,DWORD timeout=15000){
    const auto until=GetTickCount64()+timeout;
    while(GetTickCount64()<until){try{if(nlohmann::json::parse(Read(file))==expected)return true;}catch(...){}Sleep(100);}
    std::cerr<<Status()<<'\n';return false;
}
int main(int argc,char** argv){
    assert(argc>=2);const fs::path sourceData=argv[1];
    assert(!Enabled({L"-direct"})&&!Enabled({L"-mod",L"RMD-MP"}));assert(Enabled({L"-direct",L"-TXT"})&&Enabled({L"-mod",L"RMD-MP",L"-txt"}));
    const auto compiled=Build(sourceData);
    assert(compiled.metadata==nlohmann::json::parse(Read(sourceData/"D2RHUD/table-metadata.json")));
    assert(nlohmann::json::parse(compiled.auxiliary)==nlohmann::json::parse(Read(sourceData/"D2RHUD/transmog_table.json")));
    const auto root=fs::temp_directory_path()/("d2rhud-auto-metadata-"+std::to_string(GetCurrentProcessId()));const auto data=root/"data",excel=data/"global/excel";
    fs::create_directories(excel);
    for(const auto& file:fs::directory_iterator(sourceData/"global/excel"))if(file.is_regular_file()&&(Lower(file.path().extension().string())==".txt"||Lower(file.path().extension().string())==".bin"))fs::copy_file(file.path(),excel/file.path().filename());
    assert(DataRoot(root,"")==data);
    const auto modData=root/"Mods/Test/Test.mpq/data";fs::create_directories(modData/"global/excel");assert(DataRoot(root,"Test")==modData);
    bool rejected=false;try{DataRoot(root,"../unsafe");}catch(...){rejected=true;}assert(rejected);
    const auto target=data/"D2RHUD/table-metadata.json";
    Start(root,"");assert(Wait(target,compiled.metadata));
    const auto written=fs::last_write_time(target);Sleep(1800);assert(fs::last_write_time(target)==written);
    // Unsaved compilation work must not get bound to old BINs merely because
    // an author edits a TXT while the game remains open.
    const auto cube=excel/"cubemain.txt";auto text=Read(cube);const auto first=text.find('\n');assert(first!=std::string::npos);text.insert(first+1,"Automatic export test: ");Write(cube,text);
    Sleep(2000);assert(nlohmann::json::parse(Read(target))==compiled.metadata);
    // An interrupted BIN rewrite cannot replace the last complete companion.
    const auto bin=excel/"itemstatcost.bin";auto raw=Read(bin);Write(bin,raw.substr(0,4));Sleep(2200);
    assert(nlohmann::json::parse(Read(target))==compiled.metadata);
    // Finishing the rewrite plus changing a source-only recipe comment must
    // refresh both the BIN hash and the exact cached source, without a restart.
    raw.back()^=1;Write(bin,raw);
    const auto updated=Build(data);assert(updated.metadata!=compiled.metadata);assert(Wait(target,updated.metadata));
    // A temporary sharing lock retries publication after it is released.
    const auto locked=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);assert(locked!=INVALID_HANDLE_VALUE);
    raw.back()^=2;Write(bin,raw);Sleep(2200);assert(nlohmann::json::parse(Read(target))==updated.metadata);CloseHandle(locked);
    const auto recovered=Build(data);assert(Wait(target,recovered.metadata));
    assert(nlohmann::json::parse(Read(data/"D2RHUD/transmog_table.json"))==nlohmann::json::parse(compiled.auxiliary));
    std::cout<<"Native automatic export matches the Python companion format; compiler batches, source comments, BIN hashes, incomplete writes, atomic replacement, locked-output retry and idle no-rewrite checks passed.\n";
}
