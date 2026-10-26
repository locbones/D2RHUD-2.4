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
std::vector<uint8_t> Item(bool legacy,unsigned quantity=1,unsigned seed=123,bool socketed=false,bool personalized=false,bool properties=false){
    Writer w;w.Add((1<<4)|(socketed?(1<<11):0)|(personalized?(1<<24):0),32);w.Add(0,3);w.Add(0,3);w.Add(0,4);w.Add(3,4);w.Add(2,4);w.Add(1,3);w.Code("Ev11");
    w.Add(socketed?1:0,3);w.Add(seed,32);w.Add(50,7);w.Add(2,4);w.Add(0,1);w.Add(0,1);
    if(personalized){for(auto c:std::string("Candy Tester"))w.Add(c,8);w.Add(0,8);}
    w.Add(0,1);if(!legacy)w.Add(quantity,9);if(socketed)w.Add(1,4);
    if(properties){w.Add(0,9);w.Add(42,8);}w.Add(511,9);w.Align();return w.bytes;
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
    size_t unchanged=0,converted=0,refused=0,pieces=0;
    for(const auto& entry:std::filesystem::directory_iterator(saves))if(entry.path().extension()==L".d2s"){
        const auto bytes=Read(entry.path());auto r=Convert(bytes,tables);
        if(r.status==Status::Unchanged)++unchanged;
        else if(r.status==Status::Converted){++converted;pieces+=r.converted;assert(Convert(r.bytes,tables).status==Status::Unchanged);}
        else ++refused;
        assert(Read(entry.path())==bytes);
    }
    std::cout<<"Read-only compiled-table/save audit: "<<unchanged<<" unchanged, "<<converted<<" convertible ("<<pieces<<" Candy Corn), "<<refused<<" safely refused. No source saves were written.\n";
}
uint8_t* image=nullptr;
std::vector<uint8_t> itemRows,statRows;std::vector<uint32_t> typeRows;
void BindDefinitions(){
    itemRows.resize(0x1b4*2);statRows.resize(0x144*512);const uint32_t count=64,stride=2;
    typeRows.assign(count*stride,0);for(uint32_t n=0;n<count;++n)typeRows[n*stride+n/32]|=uint32_t(1)<<(n%32);
    memcpy(itemRows.data()+0x80,"Ev11",4);itemRows[0x13a]=1;memcpy(itemRows.data()+0x1b4+0x80,"r01 ",4);
    for(size_t n=0;n<512;++n)Put(statRows.data(),n*0x144,uint16_t(n));statRows[0x15]=8;statRows[0xa]=10;statRows[194*0x144+0x15]=4;
    Put(image,0x1ca0390,itemRows.data());Put(image,0x1ca0398,uint32_t(2));Put(image,0x1c9fb78,statRows.data());Put(image,0x1c9fb80,uint32_t(512));
    Put(image,0x1c9fc10,typeRows.data());Put(image,0x1c9fbf8,count);Put(image,0x1c9fc08,stride);
}
uint8_t* expectedSource=nullptr;size_t expectedSize=0;unsigned leafCalls=0;int leafResult=0;
int __fastcall MockLeaf(D2GameStrc*,D2ClientStrc*,uint8_t* bytes,int32_t size,D2UnitStrc** output){
    assert(bytes!=expectedSource && size>int(expectedSize));auto r=Convert({bytes,size_t(size)},Definitions());assert(r.status==Status::Unchanged);
    assert(Detail::Word({bytes,size_t(size)},12)==Checksum({bytes,size_t(size)}));++leafCalls;*output=reinterpret_cast<D2UnitStrc*>(1);return leafResult;
}
void NativeTests(const std::filesystem::path& exe,const std::filesystem::path& root){
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
    originalLoad=MockLeaf;
    auto original=Simple(true);expectedSize=original.size();auto allocated=std::make_unique<uint8_t[]>(original.size());memcpy(allocated.get(),original.data(),original.size());expectedSource=allocated.get();
    std::array<uint8_t,0x18> descriptor{};Put(descriptor.data(),0,allocated.get());Put(descriptor.data(),8,uint64_t(original.size()));Put(descriptor.data(),16,uint64_t(original.size()));
    D2ClientStrc client{};client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(descriptor.data());client.nSaveHeaderSize=1;D2UnitStrc* unit=nullptr;
    {
        LoadScope scope(&client,&unit);assert(active && active->converted.size()>original.size());
        const auto backup=active->backup;assert(Read(backup)==original);
        assert(client.pSaveHeaders==reinterpret_cast<D2SaveHeadersStrc*>(descriptor.data()) && Field<uint8_t*>(descriptor.data())==expectedSource);
        assert(LoadHook(nullptr,&client,expectedSource,int32_t(original.size()),&unit)==0 && leafCalls==1);
        // The native outer loader frees its ORIGINAL entry before returning.
        // Simulate that now; Scope completion/destruction must not touch it.
        assert(!memcmp(allocated.get(),original.data(),original.size()));allocated.reset();client.pSaveHeaders=nullptr;client.nSaveHeaderSize=0;
        scope.Complete(true);assert(Read(backup)==original);
    }
    assert(!active);
    auto newSave=Simple(false,72);Put(descriptor.data(),0,newSave.data());Put(descriptor.data(),8,uint64_t(newSave.size()));client.pSaveHeaders=reinterpret_cast<D2SaveHeadersStrc*>(descriptor.data());client.nSaveHeaderSize=1;
    {LoadScope scope(&client,&unit);assert(!active);}
    // Distinguish native rejection, post-reader failure and strict context
    // mismatch without replacing the original descriptor or save bytes.
    expectedSource=original.data();Put(descriptor.data(),0,original.data());Put(descriptor.data(),8,uint64_t(original.size()));
    leafResult=6;
    {LoadScope scope(&client,&unit);assert(active);const auto backup=active->backup;
        assert(LoadHook(nullptr,&client,original.data(),int32_t(original.size()),&unit)==6);
        scope.Complete(false,6);
        assert(CandyCornSave::Status().find("native reader rejected the converted copy; reader result=6; load result=6")!=std::string::npos);
        assert(Read(backup)==original&&Field<uint8_t*>(descriptor.data())==original.data());}
    leafResult=0;
    {LoadScope scope(&client,&unit);assert(active);
        assert(LoadHook(nullptr,&client,original.data(),int32_t(original.size()),&unit)==0);
        scope.Complete(false,14);
        assert(CandyCornSave::Status().find("native reader accepted the converted copy")!=std::string::npos&&CandyCornSave::Status().find("load result=14")!=std::string::npos);}
    originalLoad=+[](D2GameStrc*,D2ClientStrc*,uint8_t* bytes,int32_t size,D2UnitStrc**)->int{assert(bytes==expectedSource&&size==int32_t(expectedSize));return 6;};
    {LoadScope scope(&client,&unit);assert(active);D2UnitStrc* other=nullptr;
        assert(LoadHook(nullptr,&client,original.data(),int32_t(original.size()),&other)==6);
        scope.Complete(false,6);
        assert(CandyCornSave::Status().find("not supplied to the native reader; reader calls=1; mismatched output slot; load result=6")!=std::string::npos);}
    originalLoad=MockLeaf;
    {LoadScope scope(&client,&unit);assert(active);scope.Complete(true,0);
        assert(CandyCornSave::Status().find("not supplied to the native reader; reader calls=0")!=std::string::npos);}
    // A denied backup prevents substitution entirely.
    auto denied=root/L"blocked-native-backup";Write(denied,original);directory=denied;Put(descriptor.data(),0,original.data());Put(descriptor.data(),8,uint64_t(original.size()));
    {LoadScope scope(&client,&unit);assert(!active && Field<uint8_t*>(descriptor.data())==original.data());}
    Uninstall();assert(!installed.load());
    testImage=0;VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::cout<<"Native signatures/mutation rejection, all 256 Huffman symbols, original native checksum validator, scoped pointer/length substitution, buffer ownership, backup refusal and reader/context/post-load failure diagnostics passed.\n";
}
}
int wmain(int argc,wchar_t** argv){assert(argc==3||argc==5);const auto root=std::filesystem::path(argv[1])/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root);Test::ParseTests();Test::StorageTests(root);Test::NativeTests(argv[2],root);if(argc==5)Test::ReadOnlySaves(argv[3],argv[4]);std::cout<<"Candy Corn save conversion tests passed.\n";}
