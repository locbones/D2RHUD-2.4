#pragma once
#include "../d2/json.hpp"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <climits>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace RunTracker {
using Json=nlohmann::ordered_json;
struct Settings {
    bool enabled=false,autoStart=true,freezeTown=true,freezeLoading=true,overlay=true;
    bool milliseconds=false,overlayItems=true,overlayArea=true,overlayWall=false,overlayLocked=false;
    int timeFormat=0,historyLimit=1000;
    float overlayScale=1.f,overlayOpacity=.8f;
    bool analysisUnits=false,analysisBroad=false,analysisTown=false;
    int analysisDays=30,analysisSamples=3;
    std::string analysisCurrency,analysisTreasure;
};
struct Item {
    uint32_t id=0,seed=0;std::string code,name,location;int quality=0,level=0,quantity=1;
    int fileIndex=-1,sockets=-1;bool ethereal=false,identified=false,stackable=false,stackableKnown=false;
};
using Storage=std::map<std::string,std::vector<Item>>;
struct Frame {
    uint64_t epoch=0,now=0;int64_t utc=0;
    std::string character,mod,areaName;
    int area=0,difficulty=-1,level=0;bool town=false,loading=false,dead=false,hardcore=false,modeKnown=true;
    Storage storage; // Only complete, positively observed containers.
};
struct Find {
    Item item;int area=0;uint64_t atMs=0;
};
struct Visit {int area=0;std::string name;uint64_t atMs=0,wallMs=0,activeMs=0;};
struct Run {
    std::string id,launch,character,mod,label,notes,endReason;
    int64_t startedUtc=0,endedUtc=0;
    int difficulty=-1,level=0,deaths=0;
    uint64_t wallMs=0,activeMs=0,townMs=0,loadingMs=0,pausedMs=0,kept=0,omitted=0;
    bool finished=false,starred=false,hardcore=false,modeKnown=false;
    std::vector<Visit> visits;std::vector<Find> items;
    Storage storageStart,storageEnd;
};
inline std::string ItemKey(const Item& i){return i.code+"/"+std::to_string(i.seed)+(i.seed?std::string{}:"/"+std::to_string(i.quality)+"/"+std::to_string(i.fileIndex));}
inline std::map<std::string,Item> Aggregate(const Storage& storage){
    std::map<std::string,Item> result;
    for(const auto& [scope,items]:storage)for(const auto& item:items){const auto key=ItemKey(item);auto at=result.find(key);if(at==result.end())result.emplace(key,item);else at->second.quantity=(std::min)(INT_MAX,at->second.quantity+(std::min)(item.quantity,INT_MAX-at->second.quantity));}return result;
}
inline std::vector<Item> Additions(const Storage& before,const Storage& after){
    const auto start=Aggregate(before);auto end=Aggregate(after);std::vector<Item> result;
    for(auto& [key,item]:end){const auto at=start.find(key);if(at!=start.end())item.quantity-=at->second.quantity;if(item.quantity>0)result.push_back(std::move(item));}return result;
}
inline Json ItemJson(const Item& i){return {{"id",i.id},{"seed",i.seed},{"code",i.code},{"name",i.name},{"location",i.location},{"quality",i.quality},{"level",i.level},{"quantity",i.quantity},{"fileIndex",i.fileIndex},{"sockets",i.sockets},{"stackable",i.stackable},{"stackableKnown",i.stackableKnown},{"ethereal",i.ethereal},{"identified",i.identified}};}
inline Item ReadItem(const Json& v){Item i;i.id=v.at("id").get<uint32_t>();i.seed=v.value("seed",uint32_t(0));i.code=v.value("code","");i.name=v.value("name","");i.location=v.value("location","");i.quality=v.value("quality",0);i.level=v.value("level",0);i.quantity=v.value("quantity",1);i.fileIndex=v.value("fileIndex",-1);i.sockets=v.value("sockets",-1);i.stackable=v.value("stackable",false);i.stackableKnown=v.value("stackableKnown",false);i.ethereal=v.value("ethereal",false);i.identified=v.value("identified",false);if(i.name.size()>1024||i.code.size()>16||i.location.size()>256||i.quantity<1||i.sockets< -1||i.sockets>255)throw std::runtime_error("Invalid tracked item");return i;}
inline Json StorageJson(const Storage& storage){Json j=Json::object();for(const auto& [scope,items]:storage){j[scope]=Json::array();for(const auto& item:items)j[scope].push_back(ItemJson(item));}return j;}
inline Storage ReadStorage(const Json& j){
    if(!j.is_object()||j.size()>128)throw std::runtime_error("Invalid storage snapshot");Storage result;size_t total=0;
    for(const auto& [scope,items]:j.items()){if(scope.size()>128||!items.is_array()||(total+=items.size())>8192)throw std::runtime_error("Storage snapshot exceeds limits");for(const auto& item:items)result[scope].push_back(ReadItem(item));if(items.empty())result[scope]={};}return result;
}
inline bool Town(int area){return area==1||area==40||area==75||area==103||area==109;}
inline std::string Duration(uint64_t ms,const Settings& settings){
    const auto seconds=ms/1000;char buffer[96]{};
    if(settings.timeFormat==2){if(settings.milliseconds)snprintf(buffer,sizeof(buffer),"%.3f s",double(ms)/1000);else snprintf(buffer,sizeof(buffer),"%llu s",seconds);}
    else if(settings.timeFormat==1 || seconds>=3600)snprintf(buffer,sizeof(buffer),"%llu:%02llu:%02llu",seconds/3600,(seconds/60)%60,seconds%60);
    else snprintf(buffer,sizeof(buffer),"%llu:%02llu",seconds/60,seconds%60);
    std::string result=buffer;if(settings.milliseconds&&settings.timeFormat!=2){snprintf(buffer,sizeof(buffer),".%03llu",ms%1000);result+=buffer;}return result;
}
inline std::string Quality(int q){switch(q){case 1:return "Low quality";case 2:return "Normal";case 3:return "Superior";case 4:return "Magic";case 5:return "Set";case 6:return "Rare";case 7:return "Unique";case 8:return "Crafted";case 9:return "Tempered";default:return "Other";}}
inline Json Encode(const Run& r){
    Json j={{"schema",1},{"id",r.id},{"launch",r.launch},{"character",r.character},{"mod",r.mod},{"label",r.label},{"notes",r.notes},{"reason",r.endReason},{"startedUtc",r.startedUtc},{"endedUtc",r.endedUtc},{"difficulty",r.difficulty},{"level",r.level},{"deaths",r.deaths},{"wallMs",r.wallMs},{"activeMs",r.activeMs},{"townMs",r.townMs},{"loadingMs",r.loadingMs},{"pausedMs",r.pausedMs},{"kept",r.kept},{"omitted",r.omitted},{"finished",r.finished},{"starred",r.starred},{"storageStart",StorageJson(r.storageStart)},{"storageEnd",StorageJson(r.storageEnd)}};
    j["hardcore"]=r.hardcore;j["modeKnown"]=r.modeKnown;
    j["visits"]=Json::array();for(const auto& v:r.visits)j["visits"].push_back({{"area",v.area},{"name",v.name},{"atMs",v.atMs},{"wallMs",v.wallMs},{"activeMs",v.activeMs}});
    j["items"]=Json::array();for(const auto& f:r.items){auto v=ItemJson(f.item);v["area"]=f.area;v["atMs"]=f.atMs;j["items"].push_back(std::move(v));}return j;
}
inline bool Decode(const Json& j,Run& r){
    try{
        if(!j.is_object()||j.value("schema",0)!=1)return false;
        Run x;x.id=j.at("id").get<std::string>();if(x.id.empty()||x.id.size()>160||x.id.find_first_not_of("0123456789-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)return false;
        x.launch=j.value("launch","");x.character=j.value("character","");x.mod=j.value("mod","");x.label=j.value("label","");x.notes=j.value("notes","");x.endReason=j.value("reason","");
        x.startedUtc=j.at("startedUtc").get<int64_t>();x.endedUtc=j.value("endedUtc",int64_t(0));x.difficulty=j.value("difficulty",-1);x.level=j.value("level",0);x.deaths=j.value("deaths",0);
        x.wallMs=j.at("wallMs").get<uint64_t>();x.activeMs=j.at("activeMs").get<uint64_t>();x.townMs=j.value("townMs",uint64_t(0));x.loadingMs=j.value("loadingMs",uint64_t(0));x.pausedMs=j.value("pausedMs",uint64_t(0));
        x.kept=j.value("kept",uint64_t(0));x.omitted=j.value("omitted",uint64_t(0));x.finished=j.value("finished",false);x.starred=j.value("starred",false);
        x.hardcore=j.value("hardcore",false);x.modeKnown=j.value("modeKnown",false);
        x.storageStart=ReadStorage(j.value("storageStart",Json::object()));x.storageEnd=ReadStorage(j.value("storageEnd",Json::object()));
        if(x.activeMs>x.wallMs||x.wallMs>3155760000000ULL||x.startedUtc<0||x.character.size()>128||x.mod.size()>128||x.label.size()>256||x.notes.size()>4096)return false;
        if(!j.at("visits").is_array()||j["visits"].size()>4096||!j.at("items").is_array()||j["items"].size()>10000)return false;
        for(const auto& v:j["visits"]){Visit a{v.at("area").get<int>(),v.at("name").get<std::string>(),v.at("atMs").get<uint64_t>(),v.at("wallMs").get<uint64_t>(),v.at("activeMs").get<uint64_t>()};if(a.name.size()>512||a.activeMs>a.wallMs)return false;x.visits.push_back(std::move(a));}
        for(const auto& v:j["items"]){Find f;f.item=ReadItem(v);f.area=v.value("area",0);f.atMs=v.value("atMs",uint64_t(0));x.items.push_back(std::move(f));}
        r=std::move(x);return true;
    }catch(...){return false;}
}
class Recorder {
    Storage known_;std::map<std::string,std::pair<int,uint64_t>> firstKept_;std::map<std::string,int> acquired_;
    uint64_t serial_=0,last_=0,epoch_=0;Frame frame_;bool stopped_=false,wasDead_=false;
    void Advance(const Frame& f){
        const auto dt=f.now>=last_?f.now-last_:0;last_=f.now;if(!current)return;
        auto& r=*current;r.wallMs+=dt;if(frame_.town)r.townMs+=dt;if(frame_.loading)r.loadingMs+=dt;if(paused)r.pausedMs+=dt;
        const auto active=(!paused&&!(settings.freezeTown&&frame_.town)&&!(settings.freezeLoading&&frame_.loading))?dt:0;r.activeMs+=active;
        if(!r.visits.empty()){r.visits.back().wallMs+=dt;r.visits.back().activeMs+=active;}
    }
public:
    Settings settings;std::string launch;std::optional<Run> current;std::vector<Run> completed;bool paused=false;
    explicit Recorder(std::string process):launch(std::move(process)){}
    const Frame& Context()const{return frame_;}
    uint64_t Epoch()const{return epoch_;}
    void Start(){
        if(!settings.enabled||!epoch_||current)return;
        Run r;r.id=std::to_string(frame_.utc)+"-"+launch+"-"+std::to_string(++serial_);r.launch=launch;r.character=frame_.character;r.mod=frame_.mod;r.difficulty=frame_.difficulty;r.level=frame_.level;r.startedUtc=frame_.utc;r.hardcore=frame_.hardcore;r.modeKnown=frame_.modeKnown;
        current=std::move(r);paused=false;stopped_=false;last_=frame_.now;
        current->storageStart=known_;current->storageEnd=known_;firstKept_.clear();acquired_.clear();
        if(frame_.area>0)current->visits.push_back({frame_.area,frame_.areaName,0,0,0});
    }
    void Finish(std::string reason){if(!current)return;current->finished=true;current->endReason=std::move(reason);current->endedUtc=frame_.utc;completed.push_back(std::move(*current));current.reset();paused=false;stopped_=true;}
    void Split(){Finish("Manual split");Start();}
    void ClearRecordedData(){
        const bool restart=current.has_value();
        current.reset();completed.clear();known_=frame_.storage;
        firstKept_.clear();acquired_.clear();paused=false;stopped_=true;last_=frame_.now;
        // Preserve the serial and session identity so a discarded run's ID
        // cannot be reused. Existing possessions become the new baseline.
        if(restart)Start();
    }
    void Tick(Frame f){
        // Account the final interval using the old session's state; loading or
        // a new character can never attach native objects to the previous run.
        Advance(f);
        if(f.epoch!=epoch_){frame_.utc=f.utc;Finish("Game ended");known_.clear();firstKept_.clear();acquired_.clear();epoch_=f.epoch;stopped_=false;wasDead_=false;}
        frame_=std::move(f);
        if(!settings.enabled){Finish("Tracking disabled");return;}
        if(!current)for(const auto& [scope,items]:frame_.storage)known_[scope]=items;
        if(!current&&!stopped_&&settings.autoStart&&epoch_)Start();
        if(!current)return;
        if(!frame_.character.empty()){current->character=frame_.character;current->level=frame_.level;current->difficulty=frame_.difficulty;if(!frame_.loading){current->hardcore=frame_.hardcore;current->modeKnown=frame_.modeKnown;}}
        if(!frame_.loading){if(frame_.dead&&!wasDead_)++current->deaths;wasDead_=frame_.dead;}
        if(frame_.area>0&&(current->visits.empty()||current->visits.back().area!=frame_.area)){
            if(current->visits.size()<4096)current->visits.push_back({frame_.area,frame_.areaName,current->wallMs,0,0});else ++current->omitted;
        }
        if(frame_.storage.empty())return;
        const auto initial=Aggregate(current->storageStart);
        for(const auto& [scope,items]:frame_.storage){
            size_t total=items.size();for(const auto& [existing,values]:current->storageEnd)if(existing!=scope)total+=values.size();
            if(total>8192||(!current->storageEnd.contains(scope)&&current->storageEnd.size()>=128)){++current->omitted;continue;}
            if(!current->storageStart.contains(scope)){
                auto baseline=items;
                for(auto& item:baseline)if(item.seed){const auto key=ItemKey(item);const auto before=initial.find(key);const auto existing=before==initial.end()?0:before->second.quantity;const auto gained=acquired_.contains(key)?acquired_.at(key):0;item.quantity-=(std::min)(item.quantity,existing+(std::min)(gained,INT_MAX-existing));}
                std::erase_if(baseline,[](const Item& i){return i.quantity<=0;});current->storageStart[scope]=std::move(baseline);
            }
            current->storageEnd[scope]=items;known_[scope]=items;
        }
        if(!frame_.storage.empty()){
            current->items.clear();current->kept=0;
            for(auto& item:Additions(current->storageStart,current->storageEnd)){
                const auto key=ItemKey(item);const auto at=firstKept_.try_emplace(key,frame_.area,current->wallMs).first;
                acquired_[key]=(std::max)(acquired_[key],item.quantity);
                current->kept+=uint64_t(item.quantity);current->items.push_back({std::move(item),at->second.first,at->second.second});
            }
        }
    }
};
struct Statistics {size_t count=0;uint64_t kept=0;double total=0,mean=0,median=0,fastest=0,slowest=0,stddev=0,perHour=0;};
inline Statistics Summarize(const std::vector<const Run*>& runs){
    Statistics s;std::vector<double> times;for(const auto* r:runs){++s.count;s.kept+=r->kept;times.push_back(double(r->activeMs)/1000);s.total+=times.back();}
    if(times.empty())return s;std::sort(times.begin(),times.end());s.fastest=times.front();s.slowest=times.back();s.mean=s.total/s.count;s.median=(times[(times.size()-1)/2]+times[times.size()/2])*.5;
    for(auto t:times)s.stddev+=(t-s.mean)*(t-s.mean);s.stddev=std::sqrt(s.stddev/s.count);if(s.total>0)s.perHour=s.kept*3600/s.total;return s;
}
inline std::string CsvCell(std::string value){const auto first=value.find_first_not_of(" \t\r\n");if(first!=value.npos&&std::string("=+-@").find(value[first])!=std::string::npos)value.insert(value.begin(),'\'');std::string out="\"";for(char c:value){if(c=='"')out+='"';out+=c;}return out+'"';}
inline std::string ExportRuns(const std::vector<Run>& runs){
    std::string text="id,character,mod,label,started_utc,active_seconds,wall_seconds,kept_quantity,deaths,reason\r\n";
    for(const auto& r:runs)text+=CsvCell(r.id)+","+CsvCell(r.character)+","+CsvCell(r.mod)+","+CsvCell(r.label)+","+std::to_string(r.startedUtc)+","+std::to_string(double(r.activeMs)/1000)+","+std::to_string(double(r.wallMs)/1000)+","+std::to_string(r.kept)+","+std::to_string(r.deaths)+","+CsvCell(r.endReason)+"\r\n";return text;
}
}
