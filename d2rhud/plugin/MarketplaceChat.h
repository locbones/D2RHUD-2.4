#pragma once
#include "MarketplaceActivity.h"
#include <functional>
#include <mutex>
#include <optional>

// Value-only local mailbox/outbox. No HTTP, inventory access, native pointers,
// background threads, or shutdown work. The host owns calls into native chat.
namespace Marketplace::Chat {
inline constexpr std::array<uint8_t,16> PacketQueuePrefix{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x81,0xec,0x50,0x02,0,0,0x48,0x8b,0x05};
inline constexpr std::array<uint8_t,24> SubmitPrefix{0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8d,0xac,0x24,0xf8,0xf9,0xff,0xff,0x48,0x81,0xec,0x08,0x07,0x00,0x00};
inline constexpr std::array<uint8_t,16> ClosePrefix{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
inline std::string Plain(std::string_view text){
    std::string out;out.reserve(text.size());
    for(size_t i=0;i<text.size();){const auto ch=uint8_t(text[i]);
        if(ch==0xc3&&i+3<text.size()&&uint8_t(text[i+1])==0xbf&&text[i+2]=='c'){i+=4;continue;}
        if(ch==0xff){i+=i+2<text.size()&&text[i+1]=='c'?3:1;continue;}
        if(ch<32||ch==127){out+=' ';++i;continue;}
        out+=ch=='|'?'/':char(ch);++i;
    }return out;
}
inline std::vector<std::string> Lines(const std::string& text,size_t maximum=220){
    std::vector<std::string> out;for(size_t at=0;at<text.size();){size_t count=(std::min)(maximum,text.size()-at);
        while(count&&at+count<text.size()&&(uint8_t(text[at+count])&0xc0)==0x80)--count;
        if(!count)throw std::runtime_error("Invalid chat text boundary.");out.push_back(text.substr(at,count));at+=count;
    }return out;
}
struct Command {enum Kind{Help,Direct,Reply}kind=Help;std::string to,message;};
inline std::optional<Command> Parse(std::string_view text){
    const auto end=text.find(' ');std::string word(text.substr(0,end));for(char& c:word)if(c>='A'&&c<='Z')c+=32;
    if(word!="/mt"&&word!="/mr"&&word!="/market")return {};
    Command out;if(word=="/market")return out;
    auto rest=end==std::string_view::npos?std::string_view():text.substr(end+1);while(!rest.empty()&&rest.front()==' ')rest.remove_prefix(1);
    if(word=="/mr"){out.kind=Command::Reply;out.message=std::string(rest);}
    else{out.kind=Command::Direct;if(rest.starts_with('"')){rest.remove_prefix(1);const auto close=rest.find('"');if(close==std::string_view::npos)throw std::runtime_error("Use /mt \"Player Name\" message");out.to=rest.substr(0,close);rest.remove_prefix(close+1);}
        else{const auto space=rest.find(' ');out.to=rest.substr(0,space);rest=space==std::string_view::npos?std::string_view():rest.substr(space+1);}
        while(!rest.empty()&&rest.front()==' ')rest.remove_prefix(1);out.message=rest;
    }
    if(out.message.empty()||out.message.size()>2048||(out.kind==Command::Direct&&(out.to.empty()||out.to.size()>64)))throw std::runtime_error("Use /mt \"Player Name\" message, or /mr message to reply.");
    return out;
}
// Native CCMD_DEBUGCHEAT packet header is three bytes; the game limits text
// to 255 bytes. Decode a bounded copy so malformed packets cannot over-read.
inline std::optional<std::string> PacketText(std::span<const uint8_t> packet){
    if(packet.size()<4||packet[0]!=0x15)return {};
    const auto text=packet.subspan(3,(std::min)(size_t(256),packet.size()-3));
    const auto end=std::find(text.begin(),text.end(),uint8_t(0));
    return std::string(reinterpret_cast<const char*>(text.data()),size_t(end-text.begin()));
}
class Bridge {
    std::mutex mutex;
    std::filesystem::path folder;
    std::string owner;bool hardcore=false,hinted=false,delivering=false;
    int64_t through=0;uint64_t polled=0,presence=0;
    Json reply=Json::object();std::vector<std::string> notices;
    std::filesystem::path Outbox(){return folder/(hardcore?L"MarketplaceChatOutbox_2.4_HC.json":L"MarketplaceChatOutbox_2.4_SC.json");}
    Json ReadOutbox(std::string& original){original=ReadSettingsFile(Outbox());
        Json value=original.empty()?Json{{"schema",1},{"game_mode","2.4"},{"mod","RMD-MP"},{"hardcore",hardcore},{"owner",owner},{"commands",Json::array()}}:Json::parse(original);
        if(value.at("schema")!=1||value.at("game_mode")!="2.4"||value.at("mod")!="RMD-MP"||value.at("hardcore")!=hardcore||value.at("owner")!=owner||!value.at("commands").is_array()||value["commands"].size()>256)throw std::runtime_error("Chat outbox belongs to another player or game mode.");return value;
    }
public:
    void Configure(const std::filesystem::path& saves,const std::string& player,bool hc){
        std::lock_guard lock(mutex);if(saves!=folder||player!=owner){folder=saves;owner=player;through=0;reply=Json::object();polled=presence=0;hinted=false;
            try{const auto old=Json::parse(ReadSettingsFile(folder/L"MarketplaceChatSeen_2.4.json"));if(old.at("owner")==owner)through=(std::max)(int64_t(0),old.at("through").get<int64_t>());}catch(...){}
        }hardcore=hc;
    }
    bool Outgoing(const std::string& text){
        // Public packets never acquire the mailbox lock: native chat display
        // can re-enter ordinary game callbacks while a mailbox tick is active.
        std::optional<Command> parsed;try{parsed=Parse(text);}catch(const std::exception& e){std::lock_guard lock(mutex);notices.push_back(std::string("[Market] ")+e.what());return true;}
        if(!parsed)return false;
        std::lock_guard lock(mutex);
        if(parsed->kind==Command::Help){notices.push_back("[Market] /mt \"Player Name\" message sends a private website message. /mr message replies to the last incoming message or trade.");return true;}
        try{
            if(owner.empty()||folder.empty())throw std::runtime_error("Link your player code and start the marketplace companion before messaging.");
            Json command={{"id",ActivityId()},{"message",parsed->message},{"hardcore",hardcore}};std::string target=parsed->to;
            if(parsed->kind==Command::Reply){if(reply.empty())throw std::runtime_error("No incoming message to reply to. Use /mt \"Player Name\" message.");
                target=reply.at("sender_name").get<std::string>();
                if(reply.at("kind")=="trade"&&!reply.at("thread").get<std::string>().empty()){command["type"]="trade_reply";command["offer_id"]=reply.at("thread");command["hardcore"]=reply.at("scope").get<std::string>().ends_with("|HC");}
                else{command["type"]="direct_message";command["to"]=reply.at("sender");}
            }else{command["type"]="direct_message";command["to"]=target;}
            std::string original;auto outbox=ReadOutbox(original);if(outbox["commands"].size()>=256)throw std::runtime_error("The private message outbox is full. Let the companion reconnect first.");
            outbox["commands"].push_back(command);SaveSettingsFile(Outbox(),original,outbox.dump());
            for(const auto& line:Lines(Plain(parsed->message)))notices.push_back("[Private -> "+Plain(target)+"] "+line);
        }catch(const std::exception& e){notices.push_back(std::string("[Market] ")+e.what());}
        return true; // Private commands are never sent to the TCP/IP game.
    }
    void Tick(uint64_t now,const std::function<void(const std::string&)>& sink){
        std::vector<std::string> output;
        std::string expectedOwner;std::filesystem::path seenPath;
        int64_t nextThrough=0;Json nextReply=Json::object();
        {
            std::lock_guard lock(mutex);
            if(delivering||now<polled+500)return;
            delivering=true;polled=now;output.swap(notices);
            const auto print=[&](const std::string& text){output.push_back(text);};
            if(!owner.empty()&&!folder.empty()){
                if(now>=presence+2000){presence=now;try{const auto path=folder/L"MarketplaceChatPresence_2.4.json";const auto before=ReadSettingsFile(path);SaveSettingsFile(path,before,Json{{"schema",1},{"owner",owner},{"at",std::time(nullptr)}}.dump());}catch(...){} }
                try{
                    const auto path=folder/(hardcore?L"MarketplaceMessages_2.4_HC.json":L"MarketplaceMessages_2.4_SC.json");
                    const auto cache=Json::parse(ReadFile(path,2*1024*1024));
                    if(cache.at("schema")!=1||cache.at("game_mode")!="2.4"||cache.at("mod")!="RMD-MP"||cache.at("hardcore")!=hardcore||cache.at("owner")!=owner||!cache.at("messages").is_array()||cache["messages"].size()>256)throw std::runtime_error("Invalid private mailbox.");
                    int printed=0;int64_t previous=0;
                    for(const auto& row:cache["messages"]){
                        const auto seq=row.at("seq").get<int64_t>();
                        if(seq<=previous||row.at("recipient")!=owner)throw std::runtime_error("Invalid private message sequence.");
                        previous=seq;if(seq<=through)continue;
                        const auto kind=ActivityText(row,"kind",16),sender=ActivityText(row,"sender_name",128);
                        const auto body=row.at("body").get<std::string>();
                        if((kind!="direct"&&kind!="trade")||body.size()>4096)throw std::runtime_error("Invalid private message.");
                        for(const auto& line:Lines(Plain(body)))print(std::string(kind=="trade"?"[Trade] ":"[Message] ")+Plain(sender)+": "+line);
                        nextReply=row;nextThrough=seq;if(++printed==8)break;
                    }
                    if(nextThrough>through){
                        expectedOwner=owner;seenPath=folder/L"MarketplaceChatSeen_2.4.json";
                        if(!hinted){print("[Market] Reply with /mr message. Send privately with /mt \"Player Name\" message.");hinted=true;}
                    }
                    const auto results=cache.value("results",Json::object());
                    if(results.is_object()&&!results.empty()){
                        std::string original;auto outbox=ReadOutbox(original);auto& commands=outbox["commands"];const auto old=commands.size();
                        for(auto it=commands.begin();it!=commands.end();){const auto id=it->at("id").get<std::string>();
                            if(!results.contains(id)){++it;continue;}
                            if(results[id].contains("error"))print("[Market] Message could not be sent: "+Plain(results[id]["error"].get<std::string>()));
                            it=commands.erase(it);
                        }
                        if(commands.size()!=old)SaveSettingsFile(Outbox(),original,outbox.dump());
                    }
                }catch(...){} // An offline/malformed cache never affects gameplay.
            }
        }
        // Native chat invokes other game callbacks. Never hold our mailbox
        // mutex across it: even a private reply or nested tick must stay safe.
        try{for(const auto& text:output)sink(text);}
        catch(...){std::lock_guard lock(mutex);delivering=false;throw;}
        std::lock_guard lock(mutex);
        if(nextThrough>through&&owner==expectedOwner&&seenPath==folder/L"MarketplaceChatSeen_2.4.json"){
            through=nextThrough;reply=std::move(nextReply);
            // A message is acknowledged only after native display returns.
            try{const auto before=ReadSettingsFile(seenPath);SaveSettingsFile(seenPath,before,Json{{"schema",1},{"owner",owner},{"through",through}}.dump());}catch(...){}
        }
        delivering=false;
    }
};
}
