#pragma once
// Temporary, format-aware Candy Corn migration. No native calls or file writes.
// Layout audited against D2R.exe SHA256 e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace CandyCornSaveData {
struct ItemDefinition {
    std::array<char,4> code{};
    bool armor=false,weapon=false,stackable=false;
    bool gold=false,tome=false,bodyPart=false,charm=false,questDifficulty=false;
    bool candyRandom=false;
};
struct StatDefinition {uint8_t saveBits=0,paramBits=0,charBits=0;int32_t saveAdd=0;bool valid=false;};
struct Tables {std::vector<ItemDefinition> items;std::vector<StatDefinition> stats;};
enum class Status {Unchanged,Converted,Unsupported,Corrupt,Ambiguous,Limit};
struct Result {Status status=Status::Corrupt;std::vector<uint8_t> bytes;size_t converted=0;std::string message;};
inline uint32_t Checksum(std::span<const uint8_t> bytes) {
    uint32_t value=0;
    for(size_t i=0;i<bytes.size();++i)value=(value<<1)+(value>>31)+(i>=12&&i<16?0:bytes[i]);
    return value;
}
namespace Detail {
// All 256 symbols, including uppercase mod item codes. Native tree RVA14badb0.
inline constexpr std::array<std::string_view,256> Codes{
    "00010111101101101","00010111111110111","00010111101101100","00010111110100010","00010111111110110","00010111111001001","00010111101001101","00010111101100111",
    "00010111110100001","00010111110001110","00010111111010111","0001011110011110","00010111111001000","00010111101111111","00010111101001100","00010111110111010",
    "00010111101100110","00010111110011001","00010111110100000","00010111110010111","00010111110001101","00010111110000110","00010111111010110","00010111111100001",
    "0001011110011011","00010111111001100","0001011110010011","00010111101010110","00010111101111110","00010111101110011","00010111101010001","0001011110001110",
    "10","00010111110101100","00010111110011101","00010111110101000","00010111110011000","00010111110010011","0001011110000011","00010111111011110",
    "00010111110010110","00010111110001011","00010111110001100","00010111111111011","00010111110000101","00010111111110010","00010111111110101","00010111111001010",
    "11111011","1111100","001100","1101101","11111010","00010110","1101111","01111",
    "000100","01110","00010111101111101","00010111101110110","00010111101110010","00010111101100001","00010111101010000","00010111101101001",
    "00010111111000101","00010111101011100","00010111111001110","00010111111011001","00010111110011111","00010111101111000","00010111110011100","00010111110101011",
    "00010111110111001","00010111110111100","0001011110000111","00010111110100100","00010111110010010","00010111111011011","0001011110000010","00010111111101011",
    "00010111110100111","00010111101100100","00010111111100111","00010111101101010","00010111110001010","00010111110001001","00010111110010001","00010111111101110",
    "00010111111111010","00010111111111101","00010111110000100","00010111110000001","00010111111011101","00010111110010100","00010111111110100","00010111111100011",
    "00010111111010001","11110","0101","01000","110001","110000","010011","11010",
    "00011","1111110","000101110","010010","11101","01101","001101","1111111",
    "11001","11011001","11100","0010","01100","00001","1101110","00000",
    "00111","0001010","11011000","00010111101100010","00010111101100000","00010111110111111","00010111101001011","00010111101001111",
    "00010111101101000","0001011110001111","00010111111000100","00010111101011101","00010111110111011","00010111111001111","00010111110101111","00010111110101110",
    "00010111111011000","00010111110101101","00010111110011110","00010111101111001","0001011110100001","0001011110100000","00010111110110101","00010111110110100",
    "00010111110101010","00010111110101001","00010111110111000","00010111110111101","00010111110011011","00010111110011010","0001011110000110","00010111110100101",
    "0001011110000101","0001011110000100","0001011110100011","0001011110100010","00010111111011010","00010111110100011","0001011110000001","0001011110000000",
    "00010111111101010","00010111111011111","00010111110100110","00010111101100101","00010111111101001","00010111111101000","00010111111100110","00010111101101011",
    "00010111111100101","00010111111100100","0001011110001101","0001011110001100","00010111110001000","00010111110001111","00010111110010000","00010111111101111",
    "00010111111111111","00010111111111110","00010111111101101","00010111111101100","00010111111111100","00010111110000111","00010111110000011","00010111110000010",
    "00010111110000000","00010111111110011","00010111111011100","00010111110010101","00010111111110001","00010111111110000","00010111111010101","00010111111010100",
    "00010111111100010","00010111111001011","00010111111010000","00010111111111001","00010111111111000","00010111111100000","00010111111010011","00010111111010010",
    "0001011110001011","0001011110001010","0001011110001001","0001011110001000","0001011110011111","0001011110011010","0001011110011001","0001011110011000",
    "0001011110011101","0001011110011100","00010111101011111","00010111101011110","00010111111001101","00010111101011011","00010111101011010","00010111101011001",
    "00010111101011000","0001011110010111","0001011110010110","0001011110010101","0001011110010100","0001011110010010","0001011110010001","0001011110010000",
    "00010111111000111","00010111111000110","00010111101010011","00010111101010010","00010111101010111","00010111111000011","00010111111000010","00010111111000001",
    "00010111111000000","00010111101010101","00010111101010100","00010111101111011","00010111101111010","00010111101111100","00010111101110101","00010111101110100",
    "00010111101110111","00010111110110111","00010111110110110","00010111110110001","00010111110110000","00010111101110001","00010111101110000","00010111101100011",
    "00010111110110011","00010111110110010","00010111101101111","00010111101101110","00010111110111110","00010111101001110","00010111101001010","0001011110100100"
};
struct HuffNode {int16_t one=-1,zero=-1,symbol=-1;};
inline constexpr auto HuffTree=[] {
    std::array<HuffNode,511> nodes{};int16_t next=1;
    for(unsigned symbol=0;symbol<Codes.size();++symbol){int16_t node=0;for(char bit:Codes[symbol]){auto& child=bit=='1'?nodes[node].one:nodes[node].zero;if(child<0)child=next++;node=child;}nodes[node].symbol=int16_t(symbol);}
    return nodes;
}();
inline uint32_t Word(std::span<const uint8_t> b,size_t at,unsigned n=4) {
    uint32_t value=0;for(unsigned i=0;i<n;++i)value|=uint32_t(b[at+i])<<(8*i);return value;
}
inline void Store(std::vector<uint8_t>& b,size_t at,uint32_t v) {for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(v>>(8*i));}
struct Budget {size_t work=0,branches=0;bool limit=false;bool Spend(size_t n=1){work+=n;return !(limit=limit||work>4000000);}};
struct Bits {
    std::span<const uint8_t> bytes;size_t pos=0;Budget* budget=nullptr;bool ok=true;
    uint32_t Read(unsigned n) {
        if(!ok||n>32||pos>bytes.size()*8||n>bytes.size()*8-pos||!budget->Spend()){ok=false;return 0;}
        uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t((bytes[(pos+i)/8]>>((pos+i)%8))&1)<<i;pos+=n;return v;
    }
    void Skip(size_t n){if(!ok||pos>bytes.size()*8||n>bytes.size()*8-pos||!budget->Spend()){ok=false;return;}pos+=n;}
    bool Align(){while(ok&&pos%8)if(Read(1))ok=false;return ok;}
    bool Mark(uint16_t value){return pos%8==0&&Read(16)==value&&ok;}
    bool Name(){for(unsigned i=0;i<32&&ok;++i)if(!Read(8))return ok;ok=false;return false;}
    bool Code(std::array<char,4>& code) {
        for(char& ch:code){int16_t node=0;bool found=false;for(unsigned n=0;n<17&&ok;++n){node=Read(1)?HuffTree[node].one:HuffTree[node].zero;if(node<0){ok=false;return false;}if(HuffTree[node].symbol>=0){ch=char(HuffTree[node].symbol);found=true;break;}}if(!found){ok=false;return false;}}
        return ok;
    }
};
inline const StatDefinition* Stat(const Tables& t,unsigned id){return id<t.stats.size()&&t.stats[id].valid?&t.stats[id]:nullptr;}
inline bool StatValue(Bits& b,const Tables& t,unsigned id) {const auto d=Stat(t,id);if(!d||d->saveBits>32||d->paramBits>32){b.ok=false;return false;}b.Skip(size_t(d->saveBits)+d->paramBits);return b.ok;}
inline bool Properties(Bits& b,const Tables& t,bool* candyRandom=nullptr) {
    for(unsigned n=0;n<4096&&b.ok;++n){const unsigned id=b.Read(9);if(id==511)return b.ok;if(!StatValue(b,t,id))return false;
        if(candyRandom && id==398)*candyRandom=true;
        if(id==17||id==48||id==50||id==52){if(!StatValue(b,t,id+1))return false;}
        else if(id==54||id==57){if(!StatValue(b,t,id+1)||!StatValue(b,t,id+2))return false;}}
    b.ok=false;return false;
}
struct Edit {size_t quantity=0,rawEnd=0,alignedEnd=0;};
enum class Kind {Items,List,Corpses,Corpse,Mercenary,Golem,End};
struct Task {Kind kind;unsigned count=0,depth=0;};
struct State {Bits bits;std::vector<Task> tasks;std::vector<Edit> edits;size_t items=0;};
// A failed continuation is independent of the edits already collected. Cache
// only failures: distinct successful edit histories must still be rejected as
// ambiguous. Include the complete task stack and item count in the identity.
inline std::vector<size_t> Continuation(const State& state) {
    std::vector<size_t> key{state.bits.pos,state.items};key.reserve(2+state.tasks.size()*3);
    for(const auto& task:state.tasks){key.push_back(size_t(task.kind));key.push_back(task.count);key.push_back(task.depth);}
    return key;
}
struct Prefix {uint32_t flags=0;unsigned quality=0,sockets=0,depth=0;const ItemDefinition* definition=nullptr;};
inline bool Header(Bits& b,const Tables& t) {
    const auto bytes=b.bytes;
    if(bytes.size()<0x2ff||!b.ok)return false;
    if(Word(bytes,0x14f)!=0x216f6f57||Word(bytes,0x279,2)!=0x5357||Word(bytes,0x2c9,2)!=0x7701)return false;
    b.pos=0x2fd*8;if(!b.Mark(0x6667))return false;
    std::array<bool,512> seen{};
    for(unsigned n=0;n<512&&b.ok;++n){const unsigned id=b.Read(9);if(id==511){if(!b.Align()||!b.Mark(0x6669))return false;b.Skip(size_t(bytes[0x2a])*8);return b.ok;}const auto s=Stat(t,id);if(!s||!s->charBits||s->charBits>32||seen[id])return false;seen[id]=true;b.Skip(s->charBits);}
    return false;
}
inline bool Gold(Bits& b){const bool large=b.Read(1)!=0;b.Skip(large?32:12);b.Skip(1);return b.ok;}
inline bool ItemPrefix(State& state,const Tables& t,unsigned depth,Prefix& p) {
    auto& b=state.bits;if(depth>16||++state.items>20000||b.pos%8)return false;p.depth=depth;p.flags=b.Read(32);b.Skip(3);
    const auto mode=b.Read(3);if(mode==3||mode==5)b.Skip(32);else{if(mode>6)return false;b.Skip(15);}
    const bool compact=(p.flags&(1u<<21))!=0,ear=(p.flags&(1u<<16))!=0;
    if(ear){if(!compact)return false;b.Skip(10);return b.Name()&&b.Align();}
    std::array<char,4> code{};if(!b.Code(code))return false;
    auto def=std::lower_bound(t.items.begin(),t.items.end(),code,[](const ItemDefinition& d,const auto& c){return d.code<c;});if(def==t.items.end()||def->code!=code)return false;p.definition=&*def;
    if(compact){if(def->gold&&!Gold(b))return false;if(def->questDifficulty){auto s=Stat(t,356);if(!s)return false;b.Skip(s->saveBits);}if(b.Read(1))b.Skip(128);return b.Align();}
    p.sockets=b.Read(3);b.Skip(32);const auto level=b.Read(7);if(!level)return false;p.quality=b.Read(4);if(!p.quality||p.quality>9)return false;
    if(b.Read(1))b.Skip(3);if(b.Read(1))b.Skip(11);
    switch(p.quality){
    case 1:case 3:b.Skip(3);break;
    case 2:if(def->charm){if(b.Read(1))b.Skip(11);else b.Skip(11);}if(def->bodyPart)b.Skip(10);if(def->tome)b.Skip(5);break;
    case 4:b.Skip(22);break;
    case 5:case 7:b.Skip(12);break;
    case 6:case 8:b.Skip(16);for(unsigned n=0;n<3;++n){if(b.Read(1))b.Skip(11);if(b.Read(1))b.Skip(11);}break;
    case 9:b.Skip(16);break;
    }
    if(p.flags&(1u<<26))b.Skip(16);
    if((p.flags&(1u<<24))&&!b.Name())return false;
    // Realm payload changed to a 16-byte string in modern saves (native200d50).
    if(b.Read(1))b.Skip(128);
    if(def->armor){auto s=Stat(t,31);if(!s)return false;b.Skip(s->saveBits);}
    if(def->armor||def->weapon){auto maximum=Stat(t,73),current=Stat(t,72);if(!maximum||!current||maximum->saveBits>32)return false;const auto raw=b.Read(maximum->saveBits);if(int64_t(raw)-maximum->saveAdd>0){b.Skip(current->saveBits);}}
    if(def->gold&&!Gold(b))return false;
    return b.ok;
}
inline bool ItemSuffix(State& state,const Tables& t,const Prefix& p,bool legacy) {
    auto& b=state.bits;const auto quantity=b.pos;
    if(p.definition->stackable&&!legacy)b.Skip(9);
    if(p.flags&(1u<<11)){auto sockets=Stat(t,194);if(!sockets)return false;const auto total=b.Read(sockets->saveBits);if(p.sockets>total)return false;}
    else if(p.sockets)return false;
    unsigned extra=0;if(p.quality==5){const auto mask=b.Read(5);for(unsigned n=0;n<5;++n)extra+=(mask>>n)&1;}
    bool candyRandom=false;if(!Properties(b,t,&candyRandom))return false;
    // RMD's normal Candy Corn receive rng_generator from automagic group 394.
    // Its old stat ID can look like quantity 398, with the random value then
    // masquerading as an unrelated stat. That interpretation loses the
    // configured automagic property even when its byte length looks correct.
    if(p.definition->candyRandom && p.quality==2 && !candyRandom)return false;
    for(unsigned n=0;n<extra;++n)if(!Properties(b,t))return false;
    if((p.flags&(1u<<26))&&!Properties(b,t))return false;
    const auto rawEnd=b.pos;if(!b.Align())return false;
    if(legacy)state.edits.push_back({quantity,rawEnd,b.pos});
    if(p.sockets)state.tasks.push_back({Kind::Items,p.sockets,p.depth+1});return b.ok;
}
inline std::vector<State> Parse(std::span<const uint8_t> bytes,const Tables& t,bool alternatives,Budget& budget,bool stash=false) {
    State first;first.bits={bytes,0,&budget,true};
    if(stash){first.bits.pos=64*8;first.tasks={{Kind::End},{Kind::List}};}
    else {if(!Header(first.bits,t))return {};first.tasks={{Kind::End},{Kind::Golem},{Kind::Mercenary},{Kind::Corpses},{Kind::List}};}
    struct Frame {State state;std::vector<size_t> key;size_t validBefore=0;bool entered=false;};
    std::vector<Frame> pending;pending.push_back({std::move(first)});std::vector<State> valid;
    std::set<std::vector<size_t>> failed;
    while(!pending.empty()&&!budget.limit){
        auto& frame=pending.back();
        if(frame.entered){if(valid.size()==frame.validBefore)failed.insert(std::move(frame.key));pending.pop_back();continue;}
        frame.key=Continuation(frame.state);
        if(failed.contains(frame.key)){pending.pop_back();continue;}
        frame.entered=true;frame.validBefore=valid.size();
        State state=std::move(frame.state);bool alive=true,split=false;
        while(alive&&!state.tasks.empty()&&budget.Spend()){
            const auto task=state.tasks.back();state.tasks.pop_back();auto& b=state.bits;
            switch(task.kind){
            case Kind::End:alive=b.pos==bytes.size()*8;break;
            case Kind::List:if(!b.Mark(0x4d4a)){alive=false;break;}state.tasks.push_back({Kind::Items,b.Read(16),task.depth});break;
            case Kind::Items:{if(!task.count)break;state.tasks.push_back({Kind::Items,task.count-1,task.depth});Prefix p;if(!ItemPrefix(state,t,task.depth,p)){alive=false;break;}if(!p.definition||(p.flags&(1u<<21)))break;
                const bool target=p.definition->stackable&&p.definition->code==std::array<char,4>{'E','v','1','1'};
                if(alternatives&&target){
                    if(++budget.branches>4096){budget.limit=true;alive=false;break;}
                    State old=state;const bool oldValid=ItemSuffix(old,t,p,true);
                    const bool currentValid=ItemSuffix(state,t,p,false);
                    if(oldValid)pending.push_back({std::move(old)});
                    if(currentValid)pending.push_back({std::move(state)});
                    split=true;alive=false;break;
                }
                alive=ItemSuffix(state,t,p,false);break;}
            case Kind::Corpses:if(!b.Mark(0x4d4a)){alive=false;break;}{const auto count=b.Read(16);if(count>16){alive=false;break;}state.tasks.push_back({Kind::Corpse,count});}break;
            case Kind::Corpse:if(task.count){b.Skip(96);state.tasks.push_back({Kind::Corpse,task.count-1});state.tasks.push_back({Kind::List});}break;
            case Kind::Mercenary:if(!(bytes[0x24]&0x20))break;if(!b.Mark(0x666a)){alive=false;break;}if(Word(bytes,0xb3))state.tasks.push_back({Kind::List});break;
            case Kind::Golem:if(!(bytes[0x24]&0x20))break;if(!b.Mark(0x666b)){alive=false;break;}{auto present=b.Read(8);if(present>1){alive=false;break;}if(present)state.tasks.push_back({Kind::Items,1});}break;
            }
            // A split moved state into child frames; do not read its cursor.
            if(split)break;
            alive=alive&&b.ok;
        }
        if(alive&&state.tasks.empty()&&state.bits.ok){valid.push_back(std::move(state));if(!alternatives||valid.size()>1)return valid;}
    }
    return valid;
}
inline std::vector<uint8_t> Splice(std::span<const uint8_t> bytes,const std::vector<Edit>& edits,bool stash=false) {
    std::vector<uint8_t> out;out.reserve(bytes.size()+edits.size()*2);size_t write=0,read=0;
    auto bit=[&](unsigned value){if(write%8==0)out.push_back(0);out.back()|=uint8_t(value<<(write%8));++write;};
    auto copy=[&](size_t end){while(read<end){bit((bytes[read/8]>>(read%8))&1);++read;}};
    for(const auto& e:edits){copy(e.quantity);bit(1);for(unsigned n=1;n<9;++n)bit(0);copy(e.rawEnd);read=e.alignedEnd;while(write%8)bit(0);}
    copy(bytes.size()*8);
    if(stash)Store(out,16,uint32_t(out.size()));
    else {Store(out,8,uint32_t(out.size()));Store(out,12,Checksum(out));}
    return out;
}
}
inline Result ConvertRecord(std::span<const uint8_t> bytes,const Tables& tables,bool stash) {
    Result result;
    if(bytes.size()<16||bytes.size()>8*1024*1024||Detail::Word(bytes,0)!=0xaa55aa55){result.message="Invalid save record header.";return result;}
    if(stash){
        if(bytes.size()<68 || Detail::Word(bytes,4)>1 || Detail::Word(bytes,16)!=bytes.size() || Detail::Word(bytes,64,2)!=0x4d4a){result.message="Invalid shared stash tab header or size.";return result;}
        if(Detail::Word(bytes,8)!=98){result.status=Status::Unsupported;result.message="Candy Corn conversion supports stash version 98 only.";return result;}
    }else {
        if(Detail::Word(bytes,4)!=98){result.status=Status::Unsupported;result.message="Candy Corn conversion supports save version 98 only.";return result;}
        if(Detail::Word(bytes,8)!=bytes.size()||Detail::Word(bytes,12)!=Checksum(bytes)){result.message="Character save size or checksum is invalid.";return result;}
    }
    // Keep item lookup bounded for large saves and mod tables. Conflicting
    // definitions for one code cannot be inferred safely from saved bytes.
    Tables prepared=tables;
    std::sort(prepared.items.begin(),prepared.items.end(),[](const auto& a,const auto& b){return a.code<b.code;});
    for(size_t n=1;n<prepared.items.size();++n){const auto& a=prepared.items[n-1];const auto& b=prepared.items[n];
        if(a.code==b.code && (a.armor!=b.armor || a.weapon!=b.weapon || a.stackable!=b.stackable || a.gold!=b.gold ||
            a.tome!=b.tome || a.bodyPart!=b.bodyPart || a.charm!=b.charm || a.questDifficulty!=b.questDifficulty || a.candyRandom!=b.candyRandom)){
            result.status=Status::Unsupported;result.message="Conflicting compiled item definitions prevent safe conversion.";return result;
        }
    }
    Detail::Budget budget;auto current=Detail::Parse(bytes,prepared,false,budget,stash);
    if(!current.empty()){result.status=Status::Unchanged;return result;}
    if(budget.limit){result.status=Status::Limit;result.message="Saved items exceed conversion work limits.";return result;}
    budget={};auto legacy=Detail::Parse(bytes,prepared,true,budget,stash);
    if(budget.limit){result.status=Status::Limit;result.message="Saved items exceed conversion work limits.";return result;}
    if(legacy.size()>1){result.status=Status::Ambiguous;result.message="Saved item layout is ambiguous; conversion was refused.";return result;}
    if(legacy.empty()||legacy[0].edits.empty()){result.message="Saved items could not be safely parsed for conversion.";return result;}
    result.bytes=Detail::Splice(bytes,legacy[0].edits,stash);Detail::Budget verify;
    if(Detail::Parse(result.bytes,prepared,false,verify,stash).empty()){result.bytes.clear();result.message="Converted items failed structural verification.";return result;}
    result.status=Status::Converted;result.converted=legacy[0].edits.size();return result;
}
inline Result Convert(std::span<const uint8_t> bytes,const Tables& tables){return ConvertRecord(bytes,tables,false);}
// A .d2i file is a sequence of independently sized tabs, each with a 64-byte
// header and one item list. Do not scan for signatures inside item payloads.
// Stash headers have gold at +12 and length at +16; there is no .d2s checksum.
inline Result ConvertStash(std::span<const uint8_t> bytes,const Tables& tables){
    Result result;
    if(bytes.empty() || bytes.size()>8*1024*1024){result.message="Invalid shared stash file size.";return result;}
    std::vector<uint8_t> converted;size_t tabs=0,count=0;
    for(size_t at=0;at<bytes.size();){
        if(++tabs>1024){result.status=Status::Limit;result.message="Shared stash exceeds conversion tab limits.";return result;}
        if(bytes.size()-at<68){result.message="Truncated shared stash tab.";return result;}
        const auto size=Detail::Word(bytes,at+16);
        if(size<68 || size>bytes.size()-at){result.message="Invalid shared stash tab length.";return result;}
        const auto tab=bytes.subspan(at,size);auto next=ConvertRecord(tab,tables,true);
        if(next.status==Status::Converted){count+=next.converted;converted.insert(converted.end(),next.bytes.begin(),next.bytes.end());}
        else if(next.status==Status::Unchanged)converted.insert(converted.end(),tab.begin(),tab.end());
        else {next.bytes.clear();next.converted=0;return next;}
        at+=size;
    }
    if(count){result.status=Status::Converted;result.converted=count;result.bytes=std::move(converted);}
    else result.status=Status::Unchanged;
    return result;
}
}
