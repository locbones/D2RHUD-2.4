#include "../d2rhud/plugin/MarketplaceChat.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace Marketplace;
void Put(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<text;}
int wmain(int argc,wchar_t** argv){
    assert(argc==2);const auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder);
    const auto path=folder/L"MarketplaceMessages_2.4_SC.json";
    Json cache={{"schema",1},{"game_mode","2.4"},{"mod","RMD-MP"},{"hardcore",false},{"owner","alice"},{"messages",Json::array()},{"results",Json::object()}};
    auto message=[](int seq,std::string body){return Json{{"id",std::string(32,'a')},{"seq",seq},{"sender","bob"},{"sender_name","Bob"},{"recipient","alice"},{"scope",""},{"kind","direct"},{"thread",std::string(64,'b')},{"body",std::move(body)}};};
    Put(path,cache.dump());unsigned reads=0,presence=0;std::vector<std::string> printed;
    Chat::Bridge bridge([](auto task){task();},[&](const auto&){++presence;},[&](const auto& file){++reads;return ReadFile(file,2*1024*1024);});
    bridge.Configure(folder,"alice",false);auto print=[&](const auto& text){printed.push_back(text);};
    bridge.Tick(1000,print);
    for(uint64_t at=1500;at<31000;at+=500)bridge.Tick(at,print);
    assert(reads==1&&presence==6&&printed.empty()); // 60 metadata polls, one content read.
    bridge.Tick(31000,print);assert(reads==2);
    cache["messages"].push_back(message(1,"Immediate update"));Put(path,cache.dump());bridge.Tick(31500,print);
    assert(reads==3&&std::count(printed.begin(),printed.end(),"[Message] Bob: Immediate update")==1);
    for(int seq=2;seq<=11;++seq)cache["messages"].push_back(message(seq,"Queued "+std::to_string(seq)));
    Put(path,cache.dump());bridge.Tick(32000,print);const auto count=printed.size();bridge.Tick(32500,print);
    assert(reads==4&&printed.size()==count+2); // Cached document still drains eight-at-a-time delivery.
    bridge.Tick(33000,print);assert(reads==4);
    Put(path,"{broken");bridge.Tick(33500,print);const auto malformedReads=reads;
    for(uint64_t at=34000;at<40000;at+=500)bridge.Tick(at,print);
    assert(reads==malformedReads&&bridge.Outgoing("/market")); // Invalid unchanged content is not re-read/parsed every tick.
    cache["messages"]=Json::array({message(12,"First")});Put(path,cache.dump());bridge.Tick(40000,print);
    const auto originalTime=std::filesystem::last_write_time(path);const auto oldSize=std::filesystem::file_size(path);const auto before=reads;
    cache["messages"]=Json::array({message(13,"Other")});Put(path,cache.dump());assert(std::filesystem::file_size(path)==oldSize);
    std::filesystem::last_write_time(path,originalTime);bridge.Tick(40500,print);assert(reads==before);
    bridge.Tick(70000,print);assert(reads==before+1&&std::count(printed.begin(),printed.end(),"[Message] Bob: Other")==1);
    bridge.Tick(70500,print);
    cache["hardcore"]=true;cache["messages"]=Json::array();Put(folder/L"MarketplaceMessages_2.4_HC.json",cache.dump());
    bridge.Configure(folder,"alice",true);bridge.Tick(71000,print);assert(reads==before+2);
    cache["owner"]="charlie";cache["hardcore"]=false;Put(path,cache.dump());
    bridge.Configure(folder,"charlie",false);bridge.Tick(72000,print);assert(reads==before+3);
    unsigned attempts=0;Chat::Bridge retry([](auto task){task();},[](const auto&){},[&](const auto& file){if(++attempts==1)throw std::runtime_error("temporary sharing violation");return ReadFile(file,2*1024*1024);});
    retry.Configure(folder,"charlie",false);retry.Tick(1000,print);retry.Tick(1500,print);assert(attempts==2);
    std::cout<<"PASS: unchanged mailbox content read once across 60 polls; 30-second fallback, prompt changed-file reads, bounded message delivery, malformed cache backoff, timestamp-preserving edits and account/mode isolation; five-second presence cadence.\n";
}
