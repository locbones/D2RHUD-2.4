#include "../d2rhud/plugin/MarketplaceChat.h"
#include <cassert>
#include <iostream>
using namespace Marketplace;
static void Put(const std::filesystem::path& p,const Json& v){SaveSettingsFile(p,ReadSettingsFile(p),v.dump());}
int wmain(int argc,wchar_t** argv){
    assert(argc==2);const auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder);
    assert(!Chat::Parse("hello")&&!Chat::Parse("/players 8")&&!Chat::Parse("/mtSomething public text"));
    const auto direct=Chat::Parse("/mt \"Player Name\" Hello é");assert(direct&&direct->kind==Chat::Command::Direct&&direct->to=="Player Name"&&direct->message=="Hello é");
    assert(Chat::Parse("/mr Thanks")->kind==Chat::Command::Reply&&Chat::Parse("/market")->kind==Chat::Command::Help);
    bool refused=false;try{Chat::Parse("/mt \"Unclosed name");}catch(...){refused=true;}assert(refused);
    assert(Chat::Plain("ÿc1Bad|markup\nmessage")=="Bad/markup message");
    std::string utf8;for(int n=0;n<400;++n)utf8+="é中";std::string restored;for(const auto& line:Chat::Lines(utf8)){assert(line.size()<=220);assert((uint8_t(line.front())&0xc0)!=0x80);restored+=line;}assert(restored==utf8);
    Chat::Bridge bridge;bridge.Configure(folder,"alice",false);std::vector<std::string> printed;auto print=[&](const auto& t){printed.push_back(t);};
    // Exercise the exact packet decoder used by the native detour. A private
    // command is consumed; public text and other opcodes keep their normal path.
    int forwarded=0;auto dispatch=[&](std::vector<uint8_t> packet){const auto text=Chat::PacketText(packet);if(text&&bridge.Outgoing(*text))return;++forwarded;};
    auto wire=[](const std::string& text){std::vector<uint8_t> p{0x15,1,0};p.insert(p.end(),text.begin(),text.end());p.push_back(0);return p;};
    dispatch(wire("normal game chat"));dispatch({0x15,1});dispatch({0x44,1,0,0});assert(forwarded==3);
    dispatch(wire("/market"));dispatch(wire("/mt \"malformed private name"));assert(forwarded==3);
    assert(!bridge.Outgoing("normal game chat"));assert(bridge.Outgoing("/mt Bonesy private text"));
    const auto path=folder/L"MarketplaceChatOutbox_2.4_SC.json";const auto packet=Json::parse(ReadSettingsFile(path));assert(packet["commands"].size()==1&&packet["commands"][0]["to"]=="Bonesy");
    auto message=Json{{"id",std::string(32,'a')},{"seq",1},{"sender","bonesy"},{"sender_name","Bonesy"},{"recipient","alice"},{"scope",""},{"kind","direct"},{"thread",std::string(64,'b')},{"body","Hello from the website"}};
    auto cache=Json{{"schema",1},{"game_mode","2.4"},{"mod","RMD-MP"},{"hardcore",false},{"owner","alice"},{"messages",Json::array({message})},{"results",Json::object()}};
    Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);bridge.Tick(3000,print);assert(std::count(printed.begin(),printed.end(),"[Message] Bonesy: Hello from the website")==1);
    const auto count=printed.size();bridge.Tick(4000,print);assert(printed.size()==count);
    Chat::Bridge restarted;restarted.Configure(folder,"alice",false);restarted.Tick(5000,print);assert(printed.size()==count); // Durable dedupe across relaunch.
    assert(bridge.Outgoing("/mr Website reply"));auto out=Json::parse(ReadSettingsFile(path));assert(out["commands"].back()["type"]=="direct_message"&&out["commands"].back()["to"]=="bonesy");
    message["seq"]=2;message["kind"]="trade";message["scope"]="tcpip|RMD-MP|2.4|HC";message["thread"]=std::string(32,'c');message["body"]="Counter-offer message";cache["messages"].push_back(message);Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);bridge.Tick(6000,print);
    assert(bridge.Outgoing("/mr Trade reply"));out=Json::parse(ReadSettingsFile(path));assert(out["commands"].back()["type"]=="trade_reply"&&out["commands"].back()["offer_id"]==std::string(32,'c')&&out["commands"].back()["hardcore"]==true);
    const auto first=packet["commands"][0]["id"].get<std::string>();cache["results"][first]={{"error","Rejected private message"}};Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);bridge.Tick(7000,print);assert(Json::parse(ReadSettingsFile(path))["commands"].size()==2);
    assert(std::count(printed.begin(),printed.end(),"[Market] Message could not be sent: Rejected private message")==1);bridge.Tick(8000,print);assert(std::count(printed.begin(),printed.end(),"[Market] Message could not be sent: Rejected private message")==1);
    cache["owner"]="another-account";message["seq"]=3;message["body"]="Private to somebody else";cache["messages"].push_back(message);Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);const auto n=printed.size();bridge.Tick(9000,print);assert(printed.size()==n);
    // Native presentation is re-entrant: it can dispatch input or tick chat.
    // The mailbox must release its lock and acknowledge only after display.
    cache["owner"]="alice";Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);
    int callbacks=0;bridge.Tick(10000,[&](const std::string&){++callbacks;assert(!bridge.Outgoing("normal callback"));assert(bridge.Outgoing("/market"));bridge.Tick(11000,print);
        assert(Json::parse(ReadSettingsFile(folder/L"MarketplaceChatSeen_2.4.json"))["through"]==2);
    });assert(callbacks>0&&Json::parse(ReadSettingsFile(folder/L"MarketplaceChatSeen_2.4.json"))["through"]==3);
    message["seq"]=4;message["kind"]="trade";message["thread"]="";message["body"]="Gift received: 40 x Gold";cache["messages"].push_back(message);Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);bridge.Tick(12000,print);
    assert(bridge.Outgoing("/mr Thank you for the gift"));out=Json::parse(ReadSettingsFile(path));assert(out["commands"].back()["type"]=="direct_message"&&out["commands"].back()["to"]=="bonesy");
    message["seq"]=5;message["category"]="search";message["body"]="Saved search: 1 new listing";cache["messages"].push_back(message);Put(folder/L"MarketplaceMessages_2.4_SC.json",cache);bridge.Tick(13000,print);
    assert(std::any_of(printed.begin(),printed.end(),[](const std::string& text){return text.starts_with("[Search] ")&&text.find("Saved search: 1 new listing")!=std::string::npos;}));
    std::cout<<"Native chat: private commands, direct/trade replies, separate saved-search labels, durable dedupe, callback re-entrancy, display-before-ack and account isolation passed.\n";
}
