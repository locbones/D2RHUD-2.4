#include "../d2rhud/plugin/CandyCornSave.cpp"
#include <cassert>
#include <iostream>
#include <map>

uintptr_t testImage=0;
std::map<uint32_t,uintptr_t> bindings;
DWORD64 Pattern::Address(uint32_t rva){return testImage?testImage+rva:bindings[rva];}
namespace Test {
using namespace CandyCornSaveData;
template<class T>void Put(void* p,size_t at,T v){memcpy(static_cast<uint8_t*>(p)+at,&v,sizeof(v));}
std::vector<uint8_t> Read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);assert(in);return {(std::istreambuf_iterator<char>(in)),{}};}
void Write(const std::filesystem::path& p,std::span<const uint8_t> b){std::ofstream out(p,std::ios::binary);out.write(reinterpret_cast<const char*>(b.data()),b.size());assert(out);}
struct Writer {
    std::vector<uint8_t> bytes;size_t pos=0;
    void Add(uint32_t v,unsigned n){for(unsigned i=0;i<n;++i){if(pos%8==0)bytes.push_back(0);bytes.back()|=((v>>i)&1)<<(pos%8);++pos;}}
    void Code(const char* code){for(unsigned i=0;i<4;++i)for(auto b:Detail::Codes[uint8_t(code[i])])Add(b-'0',1);}
    void Align(){while(pos%8)Add(0,1);}
};
std::vector<uint8_t> Item(bool legacy,unsigned quantity=1,unsigned seed=123,bool socketed=false,bool personalized=false,bool properties=false,int random=-1){
    Writer w;w.Add((1<<4)|(socketed?(1<<11):0)|(personalized?(1<<24):0),32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(3,4);w.Add(2,4);w.Add(1,3);w.Code("Ev11");
    w.Add(socketed?1:0,3);w.Add(seed,32);w.Add(50,7);w.Add(2,4);w.Add(0,1);w.Add(0,1);
    if(personalized){for(auto c:std::string("Candy Tester"))w.Add(c,8);w.Add(0,8);}
    w.Add(0,1);if(!legacy)w.Add(quantity,9);if(socketed)w.Add(1,4);
    if(properties){w.Add(0,9);w.Add(42,8);}if(random>=0){w.Add(398,9);w.Add(unsigned(random)+10,11);}w.Add(511,9);w.Align();return w.bytes;
}
std::vector<uint8_t> Compact(const char* code){Writer w;w.Add((1<<21)|(1<<4),32);w.Add(0,3);w.Add(6,3);w.Add(0,15);w.Code(code);w.Align();return w.bytes;}
struct Save {
    Writer w;
    explicit Save(unsigned skills=33,bool merc=false){
        w.bytes.resize(0x2ff);w.pos=w.bytes.size()*8;
        Put(w.bytes.data(),0,uint32_t(0xaa55aa55));Put(w.bytes.data(),4,uint32_t(98));w.bytes[0x24]=0x21;w.bytes[0x2a]=uint8_t(skills);w.bytes[0x28]=1;
        const char name[]="Candy Tester";memcpy(w.bytes.data()+0x10b,name,sizeof(name));
        Put(w.bytes.data(),0x14f,uint32_t(0x216f6f57));Put(w.bytes.data(),0x279,uint16_t(0x5357));Put(w.bytes.data(),0x2c9,uint16_t(0x7701));Put(w.bytes.data(),0x2fd,uint16_t(0x6667));
        if(merc)Put(w.bytes.data(),0xb3,uint32_t(99));
        w.Add(511,9);w.Align();w.Add(0x6669,16);for(unsigned n=0;n<skills;++n)w.Add(n%3,8);
    }
    void List(unsigned count){w.Add(0x4d4a,16);w.Add(count,16);}
    void ItemBytes(std::span<const uint8_t> b){assert(!(w.pos%8));w.bytes.insert(w.bytes.end(),b.begin(),b.end());w.pos+=b.size()*8;}
    void Corpse(unsigned count){List(count);if(count)for(unsigned i=0;i<12;++i)w.Add(i,8);}
    void Merc(){w.Add(0x666a,16);}
    void Golem(bool present=false){w.Add(0x666b,16);w.Add(present,8);}
    std::vector<uint8_t> Done(){Detail::Store(w.bytes,8,uint32_t(w.bytes.size()));Detail::Store(w.bytes,12,Checksum(w.bytes));return w.bytes;}
};
Tables Definitions(){
    Tables t;t.items.push_back({{'E','v','1','1'},false,false,true});t.items.push_back({{'r','0','1',' '}});t.stats.resize(512);
    t.stats[0]={8,0,10,0,true};t.stats[194]={4,0,0,0,true};return t;
}
std::vector<uint8_t> Simple(bool legacy,unsigned quantity=1){Save s;s.List(1);s.ItemBytes(Item(legacy,quantity));s.Corpse(0);s.Merc();s.Golem();return s.Done();}
std::vector<uint8_t> Stash(bool legacy,unsigned quantity=57,bool empty=false){
    Writer w;w.bytes.resize(64);w.pos=64*8;
    Put(w.bytes.data(),0,uint32_t(0xaa55aa55));Put(w.bytes.data(),4,uint32_t(1));Put(w.bytes.data(),8,uint32_t(98));
    Put(w.bytes.data(),12,uint32_t(25000000));
    for(size_t at=20;at<64;++at)w.bytes[at]=uint8_t(at*31); // Opaque tab metadata.
    w.Add(0x4d4a,16);w.Add(empty?0:3,16);
    if(!empty)for(const auto& item:{Item(legacy,1,123,true,true,true),Item(legacy,1,456),Item(false,quantity,789),Compact("r01 ")}){
        w.bytes.insert(w.bytes.end(),item.begin(),item.end());w.pos+=item.size()*8;
    }
    Detail::Store(w.bytes,16,uint32_t(w.bytes.size()));return w.bytes;
}
std::vector<uint8_t> SocketedBelt(unsigned children,unsigned total){
    Writer w;w.Add((1<<4)|(1<<11)|(1<<26),32);w.Add(5,3);w.Add(0,3);w.Add(0,4);w.Add(1,4);w.Add(2,4);w.Add(5,3);w.Code("l07 ");
    w.Add(children,3);w.Add(0xd9dcc933,32);w.Add(119,7);w.Add(3,4);w.Add(0,1);w.Add(0,1);w.Add(3,3);w.Add(0x5073,16);w.Add(0,1);
    w.Add(86,11);w.Add(12,8);w.Add(11,9);w.Add(total,4);
    for(unsigned list=0;list<2;++list){w.Add(0,9);w.Add(42+list,8);w.Add(511,9);}w.Align();
    for(unsigned n=0;n<children;++n){auto child=Compact("r01 ");w.bytes.insert(w.bytes.end(),child.begin(),child.end());}
    return w.bytes;
}
void SocketedGearTests(){
    auto tables=Definitions();tables.items.push_back({{'l','0','7',' '},true});
    tables.stats[31]={11,0,0,10,true};tables.stats[72]={9,0,0,0,true};tables.stats[73]={8,0,0,0,true};
    for(unsigned children=0;children<8;++children)for(unsigned total=0;total<16;++total){
        const auto belt=SocketedBelt(children,total);
        Save oldSave,newSave;oldSave.List(2);newSave.List(2);
        oldSave.ItemBytes(belt);newSave.ItemBytes(belt);oldSave.ItemBytes(Item(true));newSave.ItemBytes(Item(false));
        for(auto* save:{&oldSave,&newSave}){save->Corpse(0);save->Merc();save->Golem();}
        const auto old=oldSave.Done(),expected=newSave.Done();const auto character=Convert(old,tables);
        assert(character.status==Status::Converted && character.converted==1 && character.bytes==expected);
        assert(Convert(expected,tables).status==Status::Unchanged);
        auto oldTab=Stash(false,57,true),newTab=oldTab;
        for(auto* tab:{&oldTab,&newTab}){Put(tab->data(),66,uint16_t(2));tab->insert(tab->end(),belt.begin(),belt.end());}
        auto before=Item(true),after=Item(false);oldTab.insert(oldTab.end(),before.begin(),before.end());newTab.insert(newTab.end(),after.begin(),after.end());
        for(auto* tab:{&oldTab,&newTab})Detail::Store(*tab,16,uint32_t(tab->size()));
        const auto original=oldTab;const auto stash=ConvertStash(oldTab,tables);
        assert(stash.status==Status::Converted && stash.converted==1 && stash.bytes==newTab && oldTab==original);
        assert(ConvertStash(newTab,tables).status==Status::Unchanged);
        // Missing child records still fail structural validation.
        if(children){auto truncated=oldTab;truncated.erase(truncated.begin()+68+belt.size()-Compact("r01 ").size(),truncated.begin()+68+belt.size());
            Detail::Store(truncated,16,uint32_t(truncated.size()));assert(ConvertStash(truncated,tables).status!=Status::Converted);}
    }
    auto unknown=Stash(false,57,true);const auto unknownItem=Compact("bad!");
    Put(unknown.data(),66,uint16_t(1));unknown.insert(unknown.end(),unknownItem.begin(),unknownItem.end());Detail::Store(unknown,16,uint32_t(unknown.size()));
    auto diagnosticPage=Stash(false,57,true);diagnosticPage.insert(diagnosticPage.end(),unknown.begin(),unknown.end());
    const auto failure=ConvertStash(diagnosticPage,tables);
    assert(failure.status==Status::Corrupt && failure.bytes.empty() && !failure.converted);
    assert(failure.message.find("Shared stash tab 2")!=std::string::npos && failure.message.find("'bad!'")!=std::string::npos);
    std::cout<<"128 socket child/stat combinations: runeword gear and children preserved in character/stash migration; missing child records refused; tab/item failure diagnostics verified.\n";
}
void StashTests(){
    const auto tables=Definitions();const auto old=Stash(true),current=Stash(false),original=old;
    auto result=ConvertStash(old,tables);assert(result.status==Status::Converted && result.converted==2 && result.bytes==current && old==original);
    assert(Detail::Word(result.bytes,12)==25000000 && Detail::Word(result.bytes,16)==result.bytes.size());
    assert(ConvertStash(result.bytes,tables).status==Status::Unchanged);
    for(unsigned quantity:{0,1,57,255,511})assert(ConvertStash(Stash(false,quantity),tables).status==Status::Unchanged);
    std::vector<uint8_t> page,expected;
    for(unsigned tab=0;tab<7;++tab){
        const auto source=tab==3?Stash(true):Stash(false,tab*23,tab%2==0);
        const auto target=tab==3?Stash(false):source;
        page.insert(page.end(),source.begin(),source.end());expected.insert(expected.end(),target.begin(),target.end());
    }
    const auto before=page;result=ConvertStash(page,tables);
    assert(result.status==Status::Converted && result.converted==2 && result.bytes==expected && page==before);
    assert(ConvertStash(expected,tables).status==Status::Unchanged);
    for(size_t length=0;length<old.size();++length)assert(ConvertStash({old.data(),length},tables).status!=Status::Converted);
    for(unsigned value:{0u,67u,UINT32_MAX}){auto bad=old;Detail::Store(bad,16,value);assert(ConvertStash(bad,tables).status==Status::Corrupt);}
    auto bad=old;Detail::Store(bad,8,97);assert(ConvertStash(bad,tables).status==Status::Unsupported);
    bad=old;bad.push_back(1);result=ConvertStash(bad,tables);assert(result.status==Status::Corrupt && result.bytes.empty() && !result.converted);
    bad=old;bad.insert(bad.end(),current.begin(),current.end()-1);result=ConvertStash(bad,tables);assert(result.status==Status::Corrupt && result.bytes.empty() && !result.converted);
    auto formatZero=old;Detail::Store(formatZero,4,0);assert(ConvertStash(formatZero,tables).status==Status::Converted);
    assert(Convert(Simple(true),tables).status==Status::Converted);
    // RMD's rng_generator property used to be mistaken for quantity 398,
    // followed by a different stat. Preserve every random value and existing
    // stack quantity, including mixed old/new records in a densely filled tab.
    auto rngTables=tables;rngTables.items[0].candyRandom=true;
    rngTables.stats[398]={11,0,0,10,true};rngTables.stats[270]={2,0,0,0,true};
    rngTables.stats[383]={2,0,0,1,true};rngTables.stats[402]={2,0,0,0,true};
    auto dense=Stash(false,57,true),denseExpected=dense;unsigned count=0,legacyCount=0;
    auto append=[](auto& bytes,const auto& item){bytes.insert(bytes.end(),item.begin(),item.end());};
    for(unsigned quantity:{0,1,92,296,398,500,511})for(bool properties:{false,true}){
        const auto item=Item(false,quantity,10000+count,false,false,properties);
        append(dense,item);append(denseExpected,item);++count;
        const auto currentSave=Simple(false,quantity),currentBefore=currentSave;
        assert(Convert(currentSave,rngTables).status==Status::Unchanged && currentSave==currentBefore);
    }
    // This valid modern quantity/stat pair also fits the old RNG layout.
    // A complete current-format save wins without invoking that tie-break.
    const auto prefixSource=Item(false,398);Detail::Budget prefixBudget;Detail::State prefixState;
    prefixState.bits={prefixSource,0,&prefixBudget,true};Detail::Prefix prefix;
    assert(Detail::ItemPrefix(prefixState,rngTables,0,prefix));Writer modern;
    for(size_t bit=0;bit<prefixState.bits.pos;++bit)modern.Add((prefixSource[bit/8]>>(bit%8))&1,1);
    modern.Add(398,9);modern.Add(270,9);modern.Add(0,2);modern.Add(511,9);modern.Align();
    Save modernSave;modernSave.List(1);modernSave.ItemBytes(modern.bytes);modernSave.Corpse(0);modernSave.Merc();modernSave.Golem();
    const auto modernBytes=modernSave.Done();assert(Convert(modernBytes,rngTables).status==Status::Unchanged);
    for(unsigned roll=1;roll<=549;++roll){const bool legacy=roll%3!=0;const unsigned quantity=roll%2?398:511;
        append(dense,Item(legacy,quantity,roll,false,false,false,int(roll)));
        append(denseExpected,Item(false,legacy?1:quantity,roll,false,false,false,int(roll)));
        ++count;legacyCount+=legacy;
    }
    for(auto* bytes:{&dense,&denseExpected}){Put(bytes->data(),66,uint16_t(count));Detail::Store(*bytes,16,uint32_t(bytes->size()));}
    const auto denseBefore=dense;result=ConvertStash(dense,rngTables);
    assert(result.status==Status::Converted && result.converted==legacyCount && result.bytes==denseExpected && dense==denseBefore);
    assert(ConvertStash(denseExpected,rngTables).status==Status::Unchanged);
    // Failure memoization must not suppress real ambiguity. Without an
    // automagic discriminator these repeated old/current interpretations
    // share the same continuation. A corrupt tail must finish without
    // exponentially retrying it; a valid tail must still report ambiguity.
    auto generic=rngTables;generic.items[0].candyRandom=false;
    auto ambiguous=Stash(false,57,true);append(ambiguous,Item(true,1,1,false,false,false,219));
    for(unsigned n=0;n<150;++n)append(ambiguous,Item(true,1,n+2,false,false,false,260));
    Put(ambiguous.data(),66,uint16_t(151));Detail::Store(ambiguous,16,uint32_t(ambiguous.size()));
    assert(ConvertStash(ambiguous,generic).status==Status::Ambiguous);
    auto corrupt=ambiguous;corrupt.push_back(0x7f);Detail::Store(corrupt,16,uint32_t(corrupt.size()));
    assert(ConvertStash(corrupt,generic).status==Status::Corrupt);
    std::cout<<"Dense mixed Candy Corn random properties/quantities, failed-continuation reuse and ambiguity rejection passed.\n";
    std::cout<<"Single/multiple stash tabs, mixed old/new quantities, nested items, gold/metadata preservation, exact tab lengths, malformed/truncated records and retry idempotence passed.\n";
}
void ParseTests(){
    auto t=Definitions();auto old=Simple(true),expected=Simple(false);auto original=old;
    auto r=Convert(old,t);assert(r.status==Status::Converted && r.converted==1 && r.bytes==expected && old==original);
    assert(Convert(r.bytes,t).status==Status::Unchanged);
    for(unsigned quantity:{0,1,2,17,255,500,511}){auto b=Simple(false,quantity);auto copy=b;r=Convert(b,t);assert(r.status==Status::Unchanged && r.bytes.empty() && b==copy);}
    // Preserve an existing stack, personalized items, properties, sockets,
    // corpse inventory, mercenary gear and iron-golem item byte for byte.
    std::vector<uint8_t> canonical;
    for(bool legacy:{false,true}){
        Save s(33,true);s.List(3);s.ItemBytes(Item(legacy,1,123,true,true,true));s.ItemBytes(Compact("r01 "));
        s.ItemBytes(Item(false,57,777));s.ItemBytes(Item(legacy,1,456,false,false,true));
        s.Corpse(1);s.List(1);s.ItemBytes(Item(legacy,1,888));s.Merc();s.List(1);s.ItemBytes(Item(legacy,1,999));s.Golem(true);s.ItemBytes(Item(legacy,1,1000));
        auto bytes=s.Done();r=Convert(bytes,t);
        if(legacy){assert(r.status==Status::Converted && r.converted==5 && r.bytes==canonical);auto again=Convert(r.bytes,t);assert(again.status==Status::Unchanged);}
        else {assert(r.status==Status::Unchanged);canonical=bytes;}
    }
    for(unsigned round=0;round<64;++round){Save before,after;before.List(16);after.List(16);size_t count=0;
        for(unsigned n=0;n<16;++n){const bool legacy=(n+round)%3==0;count+=legacy;const auto quantity=legacy?1:(n*31+round)%512;
            before.ItemBytes(Item(legacy,quantity,n*317+round,false,n%2==0,n%3==0));after.ItemBytes(Item(false,quantity,n*317+round,false,n%2==0,n%3==0));}
        before.Corpse(0);before.Merc();before.Golem();after.Corpse(0);after.Merc();after.Golem();
        const auto source=before.Done(),target=after.Done();r=Convert(source,t);assert(r.status==Status::Converted && r.converted==count && r.bytes==target);
    }
    // Verify variable skill sections, including a zero-skill extended class.
    for(unsigned count:{0,30,33,60,255}){Save s(count);s.List(1);s.ItemBytes(Item(true));s.Corpse(0);s.Merc();s.Golem();assert(Convert(s.Done(),t).status==Status::Converted);}
    for(size_t length=0;length<old.size();++length){auto b=std::vector<uint8_t>(old.begin(),old.begin()+length);if(length>=16){Detail::Store(b,8,uint32_t(b.size()));Detail::Store(b,12,Checksum(b));}assert(Convert(b,t).status!=Status::Converted);}
    auto b=old;b.back()=3;Detail::Store(b,12,Checksum(b));assert(Convert(b,t).status==Status::Corrupt);
    b=old;b[4]=99;assert(Convert(b,t).status==Status::Unsupported);b=old;b[50]^=1;assert(Convert(b,t).status==Status::Corrupt);
    auto missing=t;missing.items.clear();assert(Convert(old,missing).status==Status::Corrupt);
    Save massive;massive.List(65535);massive.Corpse(0);massive.Merc();massive.Golem();assert(Convert(massive.Done(),t).status!=Status::Converted);
    std::cout<<"Old/new/mixed Candy Corn, quantities, item identity/properties, nested sockets, corpses, mercenary/golem, variable skills, truncation and checksum checks passed.\n";
}
void StorageTests(const std::filesystem::path& root){
    using namespace CandyCornSave::Storage;
    auto old=Simple(true);std::filesystem::path path;std::string error;
    auto dir=root/L"Unicode-\u03a9-\u65e5\u672c";assert(Backup(dir,old,path,error));assert(Read(path)==old);
    assert(Backup(dir,old,path,error));assert(Read(path)==old);
    std::filesystem::path stashPath;auto stash=Stash(true);assert(Backup(dir,stash,stashPath,error,true) && stashPath.extension()==L".d2i" && Read(stashPath)==stash);
    auto damaged=old;damaged.back()^=1;Write(path,damaged);assert(!Backup(dir,old,path,error));assert(Read(path)==damaged);
    auto blocked=root/L"not-a-directory";Write(blocked,old);assert(!Backup(blocked,old,path,error));assert(Read(blocked)==old);
    std::cout<<"Durable Unicode backups, retry reuse, mismatch preservation and blocked backup paths passed.\n";
}
void ReadOnlySaves(const std::filesystem::path& excel,const std::filesystem::path& saves){
    std::vector<uint8_t> items;
    for(const auto name:{L"weapons.bin",L"armor.bin",L"misc.bin"}){auto b=Read(excel/name);assert(b.size()>=4 && b.size()==4+size_t(Detail::Word(b,0))*0x1b4);items.insert(items.end(),b.begin()+4,b.end());}
    auto stats=Read(excel/L"itemstatcost.bin"),types=Read(excel/L"itemtypes.bin");assert(stats.size()>=4 && types.size()>=4);
    const uint32_t count=Detail::Word(types,0),stride=(count+31)/32;assert(types.size()==4+size_t(count)*0xe4);
    std::vector<uint32_t> bits(size_t(count)*stride);
    for(uint32_t n=0;n<count;++n){std::vector<uint32_t> pending{n};while(!pending.empty()){const auto type=pending.back();pending.pop_back();auto& word=bits[size_t(n)*stride+type/32];const auto mask=uint32_t(1)<<(type%32);if(word&mask)continue;word|=mask;
        for(unsigned offset:{4,6}){const auto parent=int16_t(Detail::Word(types,4+size_t(type)*0xe4+offset,2));if(parent>0 && parent<int(count))pending.push_back(uint32_t(parent));}}}
    Tables tables;assert(DecodeTables(items,{stats.data()+4,stats.size()-4},bits,count,stride,tables));
    size_t unchanged=0,converted=0,refused=0,pieces=0,stashes=0;
    for(const auto& entry:std::filesystem::directory_iterator(saves))if(entry.path().extension()==L".d2s" || entry.path().extension()==L".d2i"){
        const bool stash=entry.path().extension()==L".d2i";stashes+=stash;
        const auto bytes=Read(entry.path());auto r=stash?ConvertStash(bytes,tables):Convert(bytes,tables);
        if(r.status==Status::Unchanged)++unchanged;
        else if(r.status==Status::Converted){++converted;pieces+=r.converted;assert((stash?ConvertStash(r.bytes,tables):Convert(r.bytes,tables)).status==Status::Unchanged);}
        else ++refused;
        assert(Read(entry.path())==bytes);
    }
    std::cout<<"Read-only compiled-table/save audit (including "<<stashes<<" stash files): "<<unchanged<<" unchanged, "<<converted<<" convertible ("<<pieces<<" Candy Corn), "<<refused<<" safely refused. No source saves were written.\n";
}
uint8_t* image=nullptr;
std::vector<uint8_t> itemRows,statRows;std::vector<uint32_t> typeRows;
void BindDefinitions(){
    itemRows.resize(0x1b4*2);statRows.resize(0x144*512);const uint32_t count=64,stride=2;
    typeRows.assign(count*stride,0);for(uint32_t n=0;n<count;++n)typeRows[n*stride+n/32]|=uint32_t(1)<<(n%32);
    memcpy(itemRows.data()+0x80,"Ev11",4);itemRows[0x13a]=1;memcpy(itemRows.data()+0x1b4+0x80,"r01 ",4);
    for(size_t n=0;n<512;++n){Put(statRows.data(),n*0x144,uint16_t(n));statRows[n*0x144+0x15]=32;}statRows[0x15]=8;statRows[0xa]=10;statRows[194*0x144+0x15]=4;
    Put(image,0x1ca0390,itemRows.data());Put(image,0x1ca0398,uint32_t(2));Put(image,0x1c9fb78,statRows.data());Put(image,0x1c9fb80,uint32_t(512));
    Put(image,0x1c9fc10,typeRows.data());Put(image,0x1c9fbf8,count);Put(image,0x1c9fc08,stride);
}
uint8_t* expectedSource=nullptr;size_t expectedSize=0;unsigned leafCalls=0;int leafResult=0;
CandyCornSave::PreviewBuffer* expectedPreviewSource=nullptr;
std::vector<uint8_t> expectedPreviewBytes;unsigned previewCalls=0;bool previewResult=true,throwPreview=false;
unsigned basicPreviewCalls=0;bool basicPreviewResult=true;
bool MockBasicPreview(CandyCornSave::PreviewBuffer* buffer){
    assert(buffer && buffer!=expectedPreviewSource && buffer->size==0x2fd);
    assert(expectedPreviewSource && !memcmp(buffer->bytes,expectedPreviewSource->bytes,0x2fd));
    assert(TryAcquireSRWLockExclusive(&expectedPreviewSource->lock));ReleaseSRWLockExclusive(&expectedPreviewSource->lock);
    ++basicPreviewCalls;return basicPreviewResult;
}
bool __fastcall MockPreview(CandyCornSave::PreviewBuffer* buffer,void* second,void* third,void* fourth,uintptr_t fifth){
    if(buffer && buffer->size==0x2fd)return MockBasicPreview(buffer);
    assert(buffer && buffer!=expectedPreviewSource && second==reinterpret_cast<void*>(2) && third==reinterpret_cast<void*>(3) && fourth==reinterpret_cast<void*>(4) && fifth==0x11223344);
    assert(buffer->size==expectedPreviewBytes.size() && std::equal(expectedPreviewBytes.begin(),expectedPreviewBytes.end(),buffer->bytes));
    assert(TryAcquireSRWLockExclusive(&expectedPreviewSource->lock));ReleaseSRWLockExclusive(&expectedPreviewSource->lock);
    assert(TryAcquireSRWLockExclusive(&buffer->lock));ReleaseSRWLockExclusive(&buffer->lock);
    ++previewCalls;if(throwPreview)throw std::runtime_error("mock preview exception");return previewResult;
}
CandyCornSave::StashDescriptor* expectedDescriptors=nullptr;
std::vector<uint8_t> expectedStash;unsigned stashCalls=0;int stashResult=0;bool throwStash=false;
std::vector<std::vector<uint8_t>> expectedRealTabs;
CandyCornSave::StashDescriptor* expectedRealDescriptors=nullptr;
int __fastcall MockStash(D2GameStrc* game,D2ClientStrc* client,D2UnitStrc* player){
    using namespace CandyCornSave;
    assert(game==reinterpret_cast<D2GameStrc*>(2) && player==reinterpret_cast<D2UnitStrc*>(3));
    assert(client->nSaveHeaderSize==3 && client->pSaveHeaders!=reinterpret_cast<D2SaveHeadersStrc*>(expectedDescriptors));
    auto descriptors=reinterpret_cast<StashDescriptor*>(client->pSaveHeaders);
    assert(!memcmp(&descriptors[0],&expectedDescriptors[0],sizeof(StashDescriptor)));
    assert(descriptors[1].bytes!=expectedDescriptors[1].bytes && descriptors[1].size==expectedStash.size() && descriptors[1].capacity==descriptors[1].size);
    assert(std::equal(expectedStash.begin(),expectedStash.end(),descriptors[1].bytes));
    assert(ConvertStash({descriptors[1].bytes,size_t(descriptors[1].size)},Definitions()).status==Status::Unchanged);
    assert(!memcmp(&descriptors[2],&expectedDescriptors[2],sizeof(StashDescriptor)));
    ++stashCalls;if(throwStash)throw std::runtime_error("mock stash exception");return stashResult;
}
int __fastcall MockLeaf(D2GameStrc*,D2ClientStrc*,uint8_t* bytes,int32_t size,D2UnitStrc** output){
    assert(bytes!=expectedSource && size>int(expectedSize));auto r=Convert({bytes,size_t(size)},Definitions());assert(r.status==Status::Unchanged);
    assert(Detail::Word({bytes,size_t(size)},12)==Checksum({bytes,size_t(size)}));++leafCalls;*output=reinterpret_cast<D2UnitStrc*>(1);return leafResult;
}
D2UnitStrc nativeStashUnit{};unsigned nativeLists=0,nativeStashUnits=0;
D2UnitStrc* __fastcall NativeStashUnit(D2GameStrc*,D2UnitStrc*,int,bool,bool){++nativeStashUnits;return &nativeStashUnit;}
int __fastcall NativeStashList(D2GameStrc*,D2UnitStrc* unit,uint8_t* bytes,int version,int size,int mode,int* consumed){
    assert(unit==&nativeStashUnit && version==98 && !mode && size>=4);
    assert(ConvertStash({bytes-64,size_t(size)+64},Definitions()).status==Status::Unchanged);
    assert(Detail::Word({bytes-64,size_t(size)+64},16)==size_t(size)+64);
    ++nativeLists;*consumed=size;return 0;
}
void Jump(uint32_t rva,void* target){auto at=image+rva;at[0]=0x48;at[1]=0xb8;Put(at,2,reinterpret_cast<uint64_t>(target));at[10]=0xff;at[11]=0xe0;}
void NativeRealSaves(const std::filesystem::path& excel,const std::filesystem::path& saves){
    using namespace CandyCornSave;
    itemRows.clear();statRows.clear();typeRows.clear();
    for(const auto name:{L"weapons.bin",L"armor.bin",L"misc.bin"}){const auto bytes=Read(excel/name);assert(bytes.size()==4+size_t(Detail::Word(bytes,0))*0x1b4);itemRows.insert(itemRows.end(),bytes.begin()+4,bytes.end());}
    const auto stats=Read(excel/L"itemstatcost.bin"),types=Read(excel/L"itemtypes.bin");
    statRows.assign(stats.begin()+4,stats.end());const auto count=Detail::Word(types,0),stride=(count+31)/32;typeRows.assign(size_t(count)*stride,0);
    for(uint32_t n=0;n<count;++n){std::vector<uint32_t> pending{n};while(!pending.empty()){auto type=pending.back();pending.pop_back();auto& word=typeRows[size_t(n)*stride+type/32];const auto mask=uint32_t(1)<<(type%32);if(word&mask)continue;word|=mask;
        for(unsigned offset:{4,6}){auto parent=int16_t(Detail::Word(types,4+size_t(type)*0xe4+offset,2));if(parent>0 && parent<int(count))pending.push_back(uint32_t(parent));}}}
    Put(image,0x1ca0390,itemRows.data());Put(image,0x1ca0398,uint32_t(itemRows.size()/0x1b4));Put(image,0x1c9fb78,statRows.data());Put(image,0x1c9fb80,uint32_t(statRows.size()/0x144));
    Put(image,0x1c9fc10,typeRows.data());Put(image,0x1c9fbf8,count);Put(image,0x1c9fc08,stride);
    Tables tables;assert(ReadTables(tables));D2ClientStrc client{};D2UnitStrc* unit=nullptr;size_t characters=0,currentCharacters=0,pieces=0;
    for(const auto& entry:std::filesystem::directory_iterator(saves))if(entry.path().extension()==L".d2s"){
        auto source=Read(entry.path());const auto before=source;const auto converted=Convert(source,tables);
        if(converted.status==Status::Unchanged){
            originalPreview=+[](PreviewBuffer* buffer,void*,void*,void*,uintptr_t)->bool{
                assert(buffer==expectedPreviewSource && buffer->bytes==expectedSource && buffer->size==expectedSize);return true;};
            originalLoad=+[](D2GameStrc*,D2ClientStrc*,uint8_t* bytes,int32_t size,D2UnitStrc**)->int{
                assert(bytes==expectedSource && size==int32_t(expectedSize));return 0;};
            PreviewBuffer buffer;buffer.bytes=source.data();buffer.size=source.size();expectedPreviewSource=&buffer;
            expectedSource=source.data();expectedSize=source.size();
            assert(Native<Preview>(0x41f570)(&buffer,nullptr,nullptr,nullptr,0));
            assert(Native<Load>(0x31d540)(nullptr,&client,source.data(),int32_t(source.size()),&unit)==0);
            assert(source==before && Read(entry.path())==before);++currentCharacters;continue;
        }
        if(converted.status!=Status::Converted)continue;
        originalPreview=MockPreview;originalLoad=+[](D2GameStrc*,D2ClientStrc*,uint8_t* bytes,int32_t size,D2UnitStrc**)->int{
            assert(bytes!=expectedSource && size==int32_t(expectedPreviewBytes.size()) && std::equal(expectedPreviewBytes.begin(),expectedPreviewBytes.end(),bytes));return 0;};
        const auto name=entry.path().stem().wstring();if(name==L"TryingAgain")assert(converted.converted==5);if(name==L"BoJenkins_a")assert(converted.converted==3);if(name==L"ToxicAF")assert(converted.converted==27);
        PreviewBuffer buffer;buffer.bytes=source.data();buffer.size=source.size();expectedPreviewSource=&buffer;expectedPreviewBytes=converted.bytes;
        assert(Native<Preview>(0x41f570)(&buffer,reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0x11223344));
        expectedSource=source.data();assert(Native<Load>(0x31d540)(nullptr,&client,source.data(),int32_t(source.size()),&unit)==0);
        assert(source==before && Read(entry.path())==before && buffer.bytes==source.data() && buffer.size==source.size());
        ++characters;pieces+=converted.converted;
    }
    std::cout<<"Real save preview and game-load detours: "<<characters<<" legacy characters, "<<pieces<<" Candy Corn; expected converted bytes and original file ownership verified.\n";
    std::cout<<"Current-format save detours: "<<currentCharacters<<" characters passed unchanged to native preview and game loading.\n";
    size_t pages=0,stashPieces=0;
    originalStashLoad=+[](D2GameStrc*,D2ClientStrc* client,D2UnitStrc*)->int{
        assert(client->pSaveHeaders!=reinterpret_cast<D2SaveHeadersStrc*>(expectedRealDescriptors));
        assert(client->nSaveHeaderSize==expectedRealTabs.size());
        auto descriptors=reinterpret_cast<StashDescriptor*>(client->pSaveHeaders);
        for(size_t n=0;n<expectedRealTabs.size();++n){const auto& expected=expectedRealTabs[n];
            assert(descriptors[n].size==expected.size() && std::equal(expected.begin(),expected.end(),descriptors[n].bytes));}
        return 0;
    };
    for(const auto& entry:std::filesystem::directory_iterator(saves))if(entry.path().extension()==L".d2i"){
        auto source=Read(entry.path());const auto before=source;const auto converted=ConvertStash(source,tables);
        if(converted.status!=Status::Converted)continue;
        if(entry.path().filename()==L"Stash_SC_Page16.d2i"){
            assert(converted.converted==23 && converted.bytes.size()==source.size()+23);
            const auto firstSize=Detail::Word(source,16),secondSize=Detail::Word(source,firstSize+16);
            assert(firstSize==1066 && secondSize==2987);
            assert(std::equal(source.begin(),source.begin()+firstSize+secondSize,converted.bytes.begin()));
        }
        std::vector<StashDescriptor> descriptors;expectedRealTabs.clear();
        for(size_t at=0;at<source.size();){const auto length=Detail::Word(source,at+16);const auto tab=std::span<const uint8_t>(source).subspan(at,length);
            const auto next=ConvertRecord(tab,tables,true);assert(next.status==Status::Converted || next.status==Status::Unchanged);
            expectedRealTabs.push_back(next.status==Status::Converted?next.bytes:std::vector<uint8_t>(tab.begin(),tab.end()));
            descriptors.push_back({source.data()+at,length,length});at+=length;
        }
        const auto originals=descriptors;expectedRealDescriptors=descriptors.data();
        client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data());client.nSaveHeaderSize=uint32_t(descriptors.size());
        assert(Native<StashLoad>(0x319b70)(nullptr,&client,nullptr)==0);
        assert(client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data()) && source==before && Read(entry.path())==before);
        assert(!memcmp(descriptors.data(),originals.data(),descriptors.size()*sizeof(StashDescriptor)));
        std::filesystem::path backup;std::string error;assert(Storage::Backup(directory,before,backup,error,true) && Read(backup)==before);
        ++pages;stashPieces+=converted.converted;
    }
    std::cout<<"Real shared-stash detours: "<<pages<<" legacy pages, "<<stashPieces<<" Candy Corn; converted tab bytes, complete original page backup and native descriptor ownership verified.\n";
}
void NativeTests(const std::filesystem::path& exe,const std::filesystem::path& root,const std::filesystem::path& excel={},const std::filesystem::path& saves={}){
    using namespace CandyCornSave;
    auto file=Read(exe);const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data()+dos->e_lfanew);
    image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);testImage=reinterpret_cast<uintptr_t>(image);
    auto sections=IMAGE_FIRST_SECTION(nt);for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n)memcpy(image+sections[n].VirtualAddress,file.data()+sections[n].PointerToRawData,sections[n].SizeOfRawData);
    assert(Supported());
    for(auto sig:signatures){image[sig.rva]^=1;assert(!Supported());image[sig.rva]^=1;}
    // Run the unmodified native Huffman decoder for every possible code byte.
    struct Bits {uint8_t* data;uint64_t bits,bytes=0,bit=0;uint32_t error=0,pad=0;};
    for(unsigned ch=0;ch<256;++ch){Writer w;char text[4]{char(ch),'v','1','1'};w.Code(text);w.Align();Bits bits{w.bytes.data(),w.bytes.size()*8};
        auto value=reinterpret_cast<uint32_t(__fastcall*)(Bits*)>(image+0x1f9530)(&bits);uint32_t expected;memcpy(&expected,text,4);assert(value==expected && !bits.error);}
    // Stop the native header reader just after validation, before game-world
    // allocations. Its original checksum/length/version/class checks all run.
    auto code=image+0x31a746;code[0]=0xb8;Put(code,1,uint32_t(0x71));code[5]=0xe9;Put(code,6,int32_t(0x31a85e-(0x31a746+10)));image[0x1434c50]=0xc3;
    std::vector<uint8_t> game(0x5700),clientData(0x1088);Put(game.data(),0x5660,uint32_t(1<<20));
    auto converted=Convert(Simple(true),Definitions()).bytes;auto cursor=converted.data();void* player=reinterpret_cast<void*>(1);
    auto readHeader=reinterpret_cast<int(__fastcall*)(void*,void*,uint8_t**,const uint8_t*,void**)>(image+0x31a580);
    assert(readHeader(game.data(),clientData.data(),&cursor,converted.data()+converted.size(),&player)==0x71 && !player);
    auto corrupt=converted;corrupt[50]^=1;cursor=corrupt.data();assert(readHeader(game.data(),clientData.data(),&cursor,corrupt.data()+corrupt.size(),&player)==6);
    BindDefinitions();
    // Initialize verifies signatures; restore the header-reader entry remains
    // unchanged (the private exit is outside the verified 32-byte entry).
    SetEnvironmentVariableW(L"LOCALAPPDATA",root.c_str());Initialize("Other");assert(!installed.load());Initialize("RMD-MP");assert(installed.load());
    const auto nativePreview=originalPreview;
    // Real native empty-buffer preview: exercise the audited five-argument ABI,
    // original shared-lock import calls and early return without creating units.
    Put(image,0x149cb50,&AcquireSRWLockShared);Put(image,0x149cb58,&ReleaseSRWLockShared);
    const auto hookedPreview=Native<Preview>(0x41f570);
    PreviewBuffer emptyPreview;assert(!hookedPreview(&emptyPreview,nullptr,nullptr,nullptr,0));
    assert(TryAcquireSRWLockExclusive(&emptyPreview.lock));ReleaseSRWLockExclusive(&emptyPreview.lock);
    originalPreview=MockPreview;
    auto previewSave=Simple(true);const auto previewBefore=previewSave;
    PreviewBuffer sourcePreview;sourcePreview.bytes=previewSave.data();sourcePreview.size=previewSave.size();expectedPreviewSource=&sourcePreview;
    expectedPreviewBytes=Convert(previewSave,Definitions()).bytes;
    assert(!active && hookedPreview(&sourcePreview,reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0x11223344));
    assert(previewCalls==1 && previewSave==previewBefore && sourcePreview.bytes==previewSave.data() && sourcePreview.size==previewSave.size());
    assert(CandyCornSave::Status().find("prepared 1 converted Candy Corn for character selection")!=std::string::npos);
    previewResult=false;assert(hookedPreview(&sourcePreview,reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0x11223344));
    assert(CandyCornSave::Status().find("native preview rejected")!=std::string::npos);
    basicPreviewResult=false;assert(!hookedPreview(&sourcePreview,reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0x11223344));basicPreviewResult=true;
    previewResult=true;throwPreview=true;assert(hookedPreview(&sourcePreview,reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0x11223344));throwPreview=false;
    assert(TryAcquireSRWLockExclusive(&sourcePreview.lock));ReleaseSRWLockExclusive(&sourcePreview.lock);
    // Converter failure must not remove a valid character header from the
    // selection menu. Never expose the unverified inventory to native parsing.
    auto badPreview=previewSave;badPreview[50]^=1;sourcePreview.bytes=badPreview.data();const auto callsBefore=previewCalls;
    assert(hookedPreview(&sourcePreview,nullptr,nullptr,nullptr,0) && previewCalls==callsBefore);
    assert(CandyCornSave::Status().find("Character remains available with a basic preview")!=std::string::npos);
    sourcePreview.bytes=previewSave.data();Put(image,0x1ca0398,uint32_t(0));
    assert(hookedPreview(&sourcePreview,nullptr,nullptr,nullptr,0) && previewCalls==callsBefore);
    assert(CandyCornSave::Status().find("tables are unavailable")!=std::string::npos);BindDefinitions();
    auto unfamiliar=Simple(false);unfamiliar.push_back(0x7e);Detail::Store(unfamiliar,8,uint32_t(unfamiliar.size()));Detail::Store(unfamiliar,12,Checksum(unfamiliar));
    PreviewBuffer unfamiliarBuffer;unfamiliarBuffer.bytes=unfamiliar.data();unfamiliarBuffer.size=unfamiliar.size();expectedPreviewSource=&unfamiliarBuffer;
    const auto unfamiliarBefore=unfamiliar;assert(hookedPreview(&unfamiliarBuffer,nullptr,nullptr,nullptr,0) && previewCalls==callsBefore && unfamiliar==unfamiliarBefore);
    expectedPreviewSource=&sourcePreview;
    // Already-current stacks and the old non-stackable mod definition retain
    // their exact native buffer/quantity rather than being converted again.
    originalPreview=+[](PreviewBuffer* buffer,void*,void*,void*,uintptr_t)->bool{assert(buffer==expectedPreviewSource);return true;};
    for(unsigned quantity:{0u,1u,500u,511u}){auto current=Simple(false,quantity);PreviewBuffer buffer;buffer.bytes=current.data();buffer.size=current.size();expectedPreviewSource=&buffer;
        assert(hookedPreview(&buffer,nullptr,nullptr,nullptr,0) && current==Simple(false,quantity));}
    itemRows[0x13a]=0;expectedPreviewSource=&sourcePreview;
    assert(hookedPreview(&sourcePreview,nullptr,nullptr,nullptr,0) && previewSave==previewBefore);BindDefinitions();
    originalPreview=nativePreview;
    const auto nativeStashLoader=originalStashLoad;
    originalLoad=MockLeaf;
    auto original=Simple(true);expectedSize=original.size();auto allocated=std::make_unique<uint8_t[]>(original.size());memcpy(allocated.get(),original.data(),original.size());expectedSource=allocated.get();
    std::array<uint8_t,0x18> descriptor{};Put(descriptor.data(),0,allocated.get());Put(descriptor.data(),8,uint64_t(original.size()));Put(descriptor.data(),16,uint64_t(original.size()));
    D2ClientStrc client{};client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(descriptor.data());client.nSaveHeaderSize=1;D2UnitStrc* unit=nullptr;
    const auto hookedReader=Native<Load>(0x31d540);
    // Real installed detour, no map/Survivor wrapper or save-descriptor vector.
    client.pSaveHeaders=nullptr;client.nSaveHeaderSize=0;
    assert(!active && hookedReader(nullptr,&client,expectedSource,int32_t(original.size()),&unit)==0 && leafCalls==1);
    assert(CandyCornSave::Status().find("converted 1 Candy Corn to quantity 1")!=std::string::npos);
    assert(!memcmp(allocated.get(),original.data(),original.size()));
    client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(descriptor.data());client.nSaveHeaderSize=1;
    {
        LoadScope scope(&client,&unit);assert(active && active->converted.empty());
        // The actual native-reader buffer and output slot are authoritative,
        // even if they differ from the outer wrapper's initial arguments.
        D2UnitStrc* readerUnit=nullptr;
        assert(hookedReader(nullptr,&client,expectedSource,int32_t(original.size()),&readerUnit)==0 && leafCalls==2);
        assert(active->count==1 && active->readerReturned && active->output==&readerUnit);
        const auto backup=active->backup;assert(Read(backup)==original);
        assert(client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptor.data()) && Field<uint8_t*>(descriptor.data())==expectedSource);
        // Native outer cleanup frees its original buffer, never our copy.
        assert(!memcmp(allocated.get(),original.data(),original.size()));allocated.reset();client.pSaveHeaders=nullptr;client.nSaveHeaderSize=0;
        scope.Complete(true,0);assert(Read(backup)==original);
    }
    assert(!active);
    auto newSave=Simple(false,72);
    expectedSource=newSave.data();expectedSize=newSave.size();
    originalLoad=+[](D2GameStrc*,D2ClientStrc*,uint8_t* bytes,int32_t size,D2UnitStrc**)->int{assert(bytes==expectedSource&&size==int32_t(expectedSize));return 0;};
    {LoadScope scope(&client,&unit);assert(hookedReader(nullptr,&client,newSave.data(),int32_t(newSave.size()),&unit)==0 && active->count==0);scope.Complete(true,0);}
    assert(hookedReader(nullptr,&client,newSave.data(),int32_t(newSave.size()),&unit)==0);
    originalLoad=MockLeaf;expectedSource=original.data();expectedSize=original.size();
    // A stale/absent descriptor cannot suppress conversion of the real input.
    client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(1);client.nSaveHeaderSize=1;
    // Native rejection and post-reader failure still report the right result.
    leafResult=6;
    {LoadScope scope(&client,&unit);
        assert(hookedReader(nullptr,&client,original.data(),int32_t(original.size()),&unit)==6);
        const auto backup=active->backup;scope.Complete(false,6);
        assert(CandyCornSave::Status().find("native reader rejected the converted copy; reader result=6; load result=6")!=std::string::npos);
        assert(Read(backup)==original);}
    leafResult=0;
    {LoadScope scope(&client,&unit);
        assert(hookedReader(nullptr,&client,original.data(),int32_t(original.size()),&unit)==0);
        scope.Complete(false,14);
        assert(CandyCornSave::Status().find("native reader accepted the converted copy")!=std::string::npos&&CandyCornSave::Status().find("load result=14")!=std::string::npos);}
    // Another client's reader must work independently of an outer scope.
    D2ClientStrc another{};
    {LoadScope scope(&client,&unit);assert(hookedReader(nullptr,&another,original.data(),int32_t(original.size()),&unit)==0 && !active->count);}
    // Exercise the installed shared-stash detour, including a preserved
    // character entry, changed tab, unchanged tab, native failure and unwind.
    const auto normalDirectory=directory;
    auto oldTab=Stash(true),emptyTab=Stash(false,57,true);const auto oldTabCopy=oldTab;
    Tables runtimeTables;assert(ReadTables(runtimeTables));assert(ConvertStash(oldTab,runtimeTables).status==Status::Converted);
    std::array<StashDescriptor,3> descriptors{{{original.data(),original.size(),original.size()},
        {oldTab.data(),oldTab.size(),oldTab.size()},{emptyTab.data(),emptyTab.size(),emptyTab.size()}}};
    const auto originalDescriptors=descriptors;
    client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data());client.nSaveHeaderSize=descriptors.size();
    expectedDescriptors=descriptors.data();expectedStash=Stash(false);originalStashLoad=MockStash;
    const auto hookedStash=Native<StashLoad>(0x319b70);
    assert(hookedStash(reinterpret_cast<D2GameStrc*>(2),&client,reinterpret_cast<D2UnitStrc*>(3))==0 && stashCalls==1);
    assert(client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data()) && !memcmp(descriptors.data(),originalDescriptors.data(),sizeof(descriptors)) && oldTab==oldTabCopy);
    assert(CandyCornSave::Status().find("Shared stash: converted 2 Candy Corn")!=std::string::npos);
    auto originalPage=oldTab;originalPage.insert(originalPage.end(),emptyTab.begin(),emptyTab.end());
    const auto pageBackup=directory/(Storage::Hex(Storage::Hash(originalPage))+L"-"+std::to_wstring(originalPage.size())+L".before-candy-corn.d2i");
    assert(Read(pageBackup)==originalPage && CandyCornSave::Status().find(DisplayPath(pageBackup))!=std::string::npos);
    stashResult=27;
    assert(hookedStash(reinterpret_cast<D2GameStrc*>(2),&client,reinterpret_cast<D2UnitStrc*>(3))==27 && stashCalls==2);
    assert(client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data()) && oldTab==oldTabCopy);
    assert(CandyCornSave::Status().find("native reader rejected the converted tabs; load result=27")!=std::string::npos);
    throwStash=true;bool caught=false;
    try{hookedStash(reinterpret_cast<D2GameStrc*>(2),&client,reinterpret_cast<D2UnitStrc*>(3));}catch(const std::runtime_error&){caught=true;}
    assert(caught && client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data()) && oldTab==oldTabCopy);throwStash=false;
    auto blockedStash=root/L"blocked-stash-backup";Write(blockedStash,oldTab);directory=blockedStash;
    const auto beforeStashCalls=stashCalls;
    assert(hookedStash(reinterpret_cast<D2GameStrc*>(2),&client,reinterpret_cast<D2UnitStrc*>(3))==27 && stashCalls==beforeStashCalls);
    assert(client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data()) && !memcmp(descriptors.data(),originalDescriptors.data(),sizeof(descriptors)) && oldTab==oldTabCopy);
    directory=normalDirectory;stashResult=0;
    // Already migrated pages retain the original descriptor vector and data.
    descriptors[1]={expectedStash.data(),expectedStash.size(),expectedStash.size()};
    originalStashLoad=+[](D2GameStrc*,D2ClientStrc* client,D2UnitStrc*)->int{assert(client->pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(expectedDescriptors));return 0;};
    assert(hookedStash(nullptr,&client,nullptr)==0);
    // Run the real native stash-loader body through its installed detour.
    // Stub only world-unit allocation/stat/network side effects and the item
    // list call, whose actual pointer/version/length contract is verified.
    descriptors[1]={oldTab.data(),oldTab.size(),oldTab.size()};
    Detail::Store(oldTab,12,0);Detail::Store(emptyTab,12,0); // Native vanilla gold cap.
    std::vector<uint8_t> stashGame(0x5700);Put(stashGame.data(),0x108,uint32_t(1));Put(stashGame.data(),0x128,uint32_t(1));
    Jump(0x319560,reinterpret_cast<void*>(NativeStashUnit));Jump(0x290d90,reinterpret_cast<void*>(NativeStashList));
    image[0x1e5270]=0x31;image[0x1e5271]=0xc0;image[0x1e5272]=0xc3;
    image[0x341670]=0xc3;image[0x2cbcb0]=0xc3;
    originalStashLoad=nativeStashLoader;
    const auto nativeOldTab=oldTab,nativeEmptyTab=emptyTab,nativeCharacter=original;
    assert(hookedStash(reinterpret_cast<D2GameStrc*>(stashGame.data()),&client,nullptr)==0);
    assert(nativeLists==2 && nativeStashUnits==3 && client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptors.data()));
    assert(oldTab==nativeOldTab && emptyTab==nativeEmptyTab && original==nativeCharacter && client.nSaveHeaderSize==3);
    client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(1);client.nSaveHeaderSize=1;
    // A denied backup returns the native bad-save result without calling the
    // reader on incompatible bytes. Test both with and without the wrapper.
    const auto calls=leafCalls;
    auto denied=root/L"blocked-native-backup";Write(denied,original);directory=denied;
    unit=reinterpret_cast<D2UnitStrc*>(1);
    assert(hookedReader(nullptr,&client,original.data(),int32_t(original.size()),&unit)==6 && leafCalls==calls && !unit);
    {LoadScope scope(&client,&unit);unit=reinterpret_cast<D2UnitStrc*>(1);assert(hookedReader(nullptr,&client,original.data(),int32_t(original.size()),&unit)==6 && active->blocked && leafCalls==calls && !unit);scope.Complete(false,6);}
    assert(CandyCornSave::Status().find("original save could not be backed up")!=std::string::npos);
    assert(original==Simple(true));
    originalPreview=MockPreview;expectedPreviewSource=&sourcePreview;expectedPreviewBytes=Simple(false);
    const auto previewsBeforeDenied=previewCalls;
    const auto basicBeforeDenied=basicPreviewCalls;
    assert(hookedPreview(&sourcePreview,reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),reinterpret_cast<void*>(4),0x11223344));
    assert(previewCalls==previewsBeforeDenied && previewSave==previewBefore && sourcePreview.bytes==previewSave.data());
    assert(basicPreviewCalls==basicBeforeDenied+1);
    if(!excel.empty()){directory=normalDirectory;NativeRealSaves(excel,saves);}
    Uninstall();assert(!installed.load());
    testImage=0;VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::cout<<"Native signatures, Huffman/checksum checks, pre-selection preview detour/ABI/locks, character/stash detours, source ownership, backup refusal, malformed/unsupported data rejection and failure diagnostics passed.\n";
}
}
int wmain(int argc,wchar_t** argv){assert(argc==3||argc==5);const auto root=std::filesystem::path(argv[1])/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root);Test::ParseTests();Test::StashTests();Test::SocketedGearTests();Test::StorageTests(root);Test::NativeTests(argv[2],root,argc==5?argv[3]:L"",argc==5?argv[4]:L"");if(argc==5)Test::ReadOnlySaves(argv[3],argv[4]);std::cout<<"Candy Corn save conversion tests passed.\n";return 0;}
