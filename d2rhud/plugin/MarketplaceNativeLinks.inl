// Uses the same native item-link viewer as chat. Raw saved bytes are copied to
// a bounded link record; the game owns preview-unit creation and destruction.
// No message is sent, and no inventory or save item is moved.
#include <map>
#include <array>
#include <string>
namespace Marketplace::NativePanel::Links {
struct Record {
    int32_t start=0,length=0;
    const char* url=nullptr;uint64_t urlLength=0,urlCapacity=0x800000000000000fULL;
    char urlInline[16]{};
    std::array<uint8_t,1024> bytes{};
    uint64_t size=0;bool item=false;uint8_t padding[7]{};
};
static_assert(sizeof(Record)==0x440&&offsetof(Record,bytes)==0x30&&offsetof(Record,item)==0x438);
struct Buffer {Record record;std::string caption;};
inline auto& Buffers(){static auto* values=new std::map<std::string,Buffer>;return *values;}
inline bool Supported(){
    static const bool supported=[](){
        for(const auto [rva,expected]:std::array<std::pair<uint32_t,uint64_t>,4>{{
            {0x5b10c0,0xaa1b9767f639542aULL},{0x622d0,0x0c539b7c817265aeULL},
            {0x5b1940,0xbee2bc857dedf710ULL},{0x5b1f00,0x6f892bafb01c7f36ULL}}}){
            std::array<uint8_t,32> code{};if(!Read(reinterpret_cast<void*>(Pattern::Address(rva)),code))return false;
            uint64_t hash=14695981039346656037ULL;for(auto byte:code){hash^=byte;hash*=1099511628211ULL;}if(hash!=expected)return false;
        }return true;
    }();return supported;
}
inline bool WidgetSupported(void* widget){
    const auto table=Field<void*>(widget);return table&&reinterpret_cast<uintptr_t>(table)==Pattern::Address(0x15b1d60)&&
        Field<uintptr_t>(table,0x18)==Pattern::Address(0x5b0f30)&&Field<uintptr_t>(table,0x20)==Pattern::Address(0x5b0fb0);
}
inline bool Build(Record& record,const std::string& caption,const std::string& hex){
    record={};record.url=record.urlInline;
    // Never truncate a linked item: socket children belong to the same record.
    if(hex.empty()||hex.size()%2||hex.size()>record.bytes.size()*2)return false;
    auto nibble=[](unsigned char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    for(size_t n=0;n<hex.size()/2;++n){const int hi=nibble(hex[n*2]),lo=nibble(hex[n*2+1]);if(hi<0||lo<0){record={};record.url=record.urlInline;return false;}record.bytes[n]=uint8_t(hi*16+lo);}
    for(unsigned char c:caption)if((c&0xc0)!=0x80)++record.length;
    record.size=hex.size()/2;record.item=record.length>0;return record.item;
}
using Assign=void*(__fastcall*)(void*,const char*,uint64_t);
inline bool BindWith(void* widget,const Panel::PreviewUpdate& update,Assign assign){
    if(!WidgetSupported(widget)||!assign||update.label.size()>1024)return false;
    auto& buffer=Buffers()[update.name];auto pointer=&buffer.record;
    const auto previous=Field<Record*>(widget,0x520);const auto count=Field<uint64_t>(widget,0x528),capacity=Field<uint64_t>(widget,0x530);
    // Only our dedicated, initially empty JSON widgets are eligible. Native
    // links from other controls retain their original allocation and lifetime.
    if(previous!=pointer&&(previous||count||capacity))return false;
    const auto caption=update.itemHex.empty()?update.label:"["+update.label+"]";
    const bool linked=Build(buffer.record,caption,update.itemHex);
    if(previous!=pointer||buffer.caption!=caption){assign(static_cast<uint8_t*>(widget)+0x5c0,caption.c_str(),caption.size());buffer.caption=caption;}
    // Negative capacity marks externally owned memory. Native destruction still
    // clears each record, but skips freeing the vector and its empty URL.
    const uint64_t length=linked?1:0,external=0x8000000000000001ULL;
    memcpy(static_cast<uint8_t*>(widget)+0x520,&pointer,sizeof(pointer));
    memcpy(static_cast<uint8_t*>(widget)+0x528,&length,sizeof(length));
    memcpy(static_cast<uint8_t*>(widget)+0x530,&external,sizeof(external));
    return linked;
}
inline bool Bind(void* widget,const Panel::PreviewUpdate& update){return Supported()&&BindWith(widget,update,reinterpret_cast<Assign>(Pattern::Address(0x622d0)));}
inline bool Inspect(void* widget){
    if(!Supported()||!WidgetSupported(widget)||Field<uint64_t>(widget,0x528)!=1)return false;
    const auto record=Field<Record*>(widget,0x520);Record value{};if(!Read(record,value)||!value.item||!value.size||value.size>1024)return false;
    reinterpret_cast<void(__fastcall*)(void*)>(Pattern::Address(0x5b1940))(widget);
    const auto bounds=Field<void*>(widget,0x738);std::array<int32_t,4> rect{};
    if(!Field<uint64_t>(widget,0x740)||!Read(bounds,rect)||rect[2]<=0||rect[3]<=0)return false;
    const uint64_t point=uint32_t(rect[0]+rect[2]/2)|(uint64_t(uint32_t(rect[1]+rect[3]/2))<<32);
    reinterpret_cast<void(__fastcall*)(void*,uint64_t)>(Pattern::Address(0x5b10c0))(widget,point);return true;
}
}
