#include "../d2rhud/plugin/ExtendedClasses.cpp"
#include <cassert>
#include <iostream>
#include <cstdlib>

using namespace ExtendedClasses;
uintptr_t testImage=0;
std::vector<uint8_t> capturedSave;
DWORD64 Pattern::Address(uint32_t rva){return testImage+rva;}
Map registry{};
void* __fastcall RegistryStub(){return &registry;}
void* __fastcall AllocateStub(void*,uint64_t size,uint64_t alignment){assert(alignment==16);return _aligned_malloc(size,16);}
uintptr_t allocatorVtable[5]{};
uintptr_t allocatorObject=0;
void* __fastcall AllocatorStub(){return &allocatorObject;}
std::array<uint32_t,10> skillCounts{};
std::vector<uint16_t> skillIndices;
void __fastcall CountsResize(uintptr_t* vector,uint64_t count){
    assert(count==8);vector[0]=reinterpret_cast<uintptr_t>(skillCounts.data()+1);vector[2]=8;
}
void __fastcall IndicesResize(uintptr_t* vector,uint64_t count){
    assert(count==16);skillIndices.resize(count+2,0xfefe);vector[0]=reinterpret_cast<uintptr_t>(skillIndices.data()+1);vector[2]=count;
}
void Jump(uint32_t rva,uintptr_t target){auto out=Native<uint8_t*>(rva);const uint8_t enter[]{0x48,0xb8};memcpy(out,enter,2);memcpy(out+2,&target,8);out[10]=0xff;out[11]=0xe0;}
template<class F> void Jump(uint32_t rva,F* function){Jump(rva,reinterpret_cast<uintptr_t>(function));}
void* RegisterThunk(uint32_t start,uint32_t stop){
    auto out=Native<uint8_t*>(0x1ff0000);
    // Save every nonvolatile register used by the native table-loader block.
    const uint8_t enter[]{0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x28,0x33,0xff,0x48,0xb8};
    memcpy(out,enter,sizeof(enter));auto address=Pattern::Address(start);memcpy(out+sizeof(enter),&address,8);
    const uint8_t loadCount[]{0x49,0xb8};auto tail=out+sizeof(enter)+8;memcpy(tail,loadCount,2);
    auto count=*Native<uint64_t*>(0x1c9faf8);memcpy(tail+2,&count,8);tail[10]=0xff;tail[11]=0xe0;
    const uint8_t leave[]{0x48,0x83,0xc4,0x28,0x41,0x5f,0x41,0x5e,0x41,0x5d,0x41,0x5c,0x5f,0x5e,0x5d,0x5b,0xc3};
    memcpy(Native<void*>(stop),leave,sizeof(leave));return out;
}
void RegistryTest(){
    // Execute the actual 2.4 enum parser and formatter against a real native
    // hash bucket layout. Only its static initialization/allocator are stubbed.
    std::array<uintptr_t,16> buckets{};registry={16,buckets.data(),7,1.f};
    allocatorVtable[1]=reinterpret_cast<uintptr_t>(&AllocateStub);allocatorObject=reinterpret_cast<uintptr_t>(allocatorVtable);
    Jump(0xacdd80,AllocatorStub);Jump(0x59df70,RegistryStub);
    ExtendRegistry(&registry);assert(registryReady && registry.count==8);
    auto parse=Native<int(__fastcall*)(void*,const char*)>(0x5aa930);
    auto format=Native<const char*(__fastcall*)(void*,int)>(0x5aa8a0);
    // Cookie validation is a leaf return; do not clobber the result register.
    *Native<uint8_t*>(0x1434c50)=0xc3;
    assert(parse(nullptr,"Warlock")==7);
    assert(std::string(format(nullptr,7))=="Warlock");
    assert(parse(nullptr,"Missing")==0);
    auto node=registry.heads[Native<uint64_t(__fastcall*)(const char*,uint64_t)>(0x5d3e0)("Warlock",7)&15];
    _aligned_free(reinterpret_cast<void*>(node));
    std::cout<<"Native class enum parser/formatter accepts Warlock as ID 7.\n";
}
void CreationInfoTest(){
    // Build the actual seven-entry creation registry and extend it using the
    // game's allocation, insertion and balancing contract.
    Jump(0xacdd80,AllocatorStub);*Native<uint8_t*>(0x1434ecc)=0xc3;
    *Native<uint8_t*>(0x1434c50)=0xc3;
    Native<void(*)()>(0x52320)();
    assert(*Native<uint64_t*>(0x1baeda8)==7);
    auto header=Native<CreationNode*>(0x1baed88);
    assert(ExtendCreationInfo() && *Native<uint64_t*>(0x1baeda8)==8);
    auto node=header->parent;
    while(node && std::string(node->key,node->length)!="Warlock")node=std::string(node->key,node->length)<"Warlock"?node->right:node->left;
    assert(node && node->info.name==27566 && node->info.description==27568 && node->info.classId==7);
    assert(header->right==node && !node->right);
    std::vector<CreationNode*> nodes;
    const auto collect=[&](auto&& self,CreationNode* n)->void{if(!n)return;self(self,n->left);nodes.push_back(n);self(self,n->right);};
    collect(collect,header->parent);assert(nodes.size()==8);
    for(size_t i=1;i<nodes.size();++i)assert(std::string(nodes[i-1]->key,nodes[i-1]->length)<std::string(nodes[i]->key,nodes[i]->length));
    for(auto n:nodes)_aligned_free(n);
    std::cout<<"Native creation registry retains seven classes and registers Warlock's name, description and creation ID.\n";
}
void InventoryTest(){
    struct Unit{uint32_t type,id;} unit{0,0};
    auto inventory=Native<int(__fastcall*)(void*,uint8_t,bool)>(0x209e40);
    const int expected[]{0,1,2,3,4,12,13,2};
    for(unsigned i=0;i<8;++i){unit.id=i;assert(inventory(&unit,0,true)==expected[i]);assert(inventory(&unit,3,true)==9);assert(inventory(&unit,1,true)==6);}
    unit.id=8;assert(inventory(&unit,0,true)==-1);
    std::cout<<"Native inventory mapping retains all seven classes and gives Warlock a valid grid.\n";
}
void SaveSkillLengthTest(bool extended){
    std::array<int32_t,8> counts{30,30,30,30,30,30,30,0};
    const auto oldCounts=*Native<uintptr_t*>(0x1c9faf0);
    const auto oldMaximum=*Native<uint8_t*>(0x1c9fb08);
    *Native<uintptr_t*>(0x1c9faf0)=reinterpret_cast<uintptr_t>(counts.data());
    *Native<uint8_t*>(0x1c9fb08)=30;
    std::array<uint8_t,8> headerTail{};memcpy(headerTail.data(),Native<void*>(0x317bbe),8);
    // Run the native header's exact skill-count instruction with its actual
    // R13 player-data input, stopping before unrelated header fields.
    const uint8_t leaveHeader[]{0x41,0x5d,0xc3};memcpy(Native<void*>(0x317bbe),leaveHeader,3);
    auto code=Native<uint8_t*>(0x1ff1000);auto out=code;
    const uint8_t enterHeader[]{0x41,0x55,0x49,0x89,0xcd,0x48,0xb8};
    memcpy(out,enterHeader,sizeof(enterHeader));out+=sizeof(enterHeader);
    auto address=Pattern::Address(0x317bb7);memcpy(out,&address,8);out+=8;*out++=0xff;*out++=0xe0;
    auto writeCount=reinterpret_cast<unsigned(*)(const void*)>(code);
    std::array<uint8_t,16> player{};
    for(unsigned id=0;id<8;++id){player[8]=uint8_t(id);assert(writeCount(player.data())==(extended && id==7?0:30));}
    counts[7]=5;assert(writeCount(player.data())==(extended?5:30));counts[7]=0;
    memcpy(Native<void*>(0x317bbe),headerTail.data(),8);

    std::array<uint8_t,32> readTailA{},readTailB{};
    memcpy(readTailA.data(),Native<void*>(0x31d921),32);memcpy(readTailB.data(),Native<void*>(0x31d9af),32);
    // Execute the native reader branch and recover the count it later adds
    // to the section cursor. Both normal/zero-count destinations return it.
    const uint8_t leaveReader[]{0x0f,0xb6,0x84,0x24,0xa8,0,0,0,0x48,0x81,0xc4,0xb8,0,0,0,0x41,0x5f,0x41,0x5d,0x5e,0x5f,0xc3};
    memcpy(Native<void*>(0x31d921),leaveReader,sizeof(leaveReader));memcpy(Native<void*>(0x31d9af),leaveReader,sizeof(leaveReader));
    code=Native<uint8_t*>(0x1ff1100);out=code;
    const uint8_t enterReader[]{0x57,0x56,0x41,0x55,0x41,0x57,0x48,0x81,0xec,0xb8,0,0,0,
        0x89,0xce,0x89,0xd7,0x4d,0x89,0xc5,0x4d,0x89,0xcf,0x40,0x88,0xbc,0x24,0xa8,0,0,0,0x48,0xb8};
    memcpy(out,enterReader,sizeof(enterReader));out+=sizeof(enterReader);
    address=Pattern::Address(0x31d918);memcpy(out,&address,8);out+=8;*out++=0xff;*out++=0xe0;
    auto readCount=reinterpret_cast<unsigned(*)(unsigned,unsigned,const uint8_t*,const uint8_t*)>(code);
    const uint8_t items[]{'J','M',7,0},skills[]{0,0,0,0};
    for(unsigned id=0;id<7;++id)assert(readCount(id,30,items,items+4)==30);
    assert(readCount(7,30,items,items+4)==(extended?0:30));
    assert(readCount(7,0,items,items+4)==0);
    assert(readCount(7,29,items,items+4)==29);
    assert(readCount(8,30,items,items+4)==30);
    assert(readCount(7,30,items,items+2)==(extended?0:30));
    assert(readCount(7,30,skills,skills+4)==30);
    assert(readCount(7,30,items,items+1)==30); // no out-of-bounds marker read
    counts[7]=30;assert(readCount(7,30,items,items+4)==30);counts[7]=0;
    if(!capturedSave.empty()){
        auto marker=std::search(capturedSave.begin()+335,capturedSave.end(),std::begin("if") ,std::end("if")-1);
        assert(marker!=capturedSave.end());auto start=&*marker+2;
        const auto count=readCount(7,capturedSave[42],start,capturedSave.data()+capturedSave.size());
        assert((start[count]=='J' && start[count+1]=='M')==(extended || capturedSave[42]==0));
    }
    memcpy(Native<void*>(0x31d921),readTailA.data(),32);memcpy(Native<void*>(0x31d9af),readTailB.data(),32);
    *Native<uintptr_t*>(0x1c9faf0)=oldCounts;*Native<uint8_t*>(0x1c9fb08)=oldMaximum;
    std::cout<<(extended?"Patched Warlock skill lengths and legacy test-save compatibility preserve the item section.":"Original Warlock skill header/body mismatch reproduced.")<<"\n";
}
void SaveHeaderTest(bool extended){
    // Execute the real current-format header reader, including its checksum,
    // version, expansion flag and class checks. Stop at the first instruction
    // AFTER validation, before it needs a live game's quests/world/allocator.
    // Before the patch, a valid Warlock save returns 9, which the outer save
    // loader maps to SYSERROR_EXPANSIONGAME (23).
    std::array<uint8_t,10> saved{};memcpy(saved.data(),Native<void*>(0x31a746),saved.size());
    auto code=Native<uint8_t*>(0x31a746);
    code[0]=0xb8;const uint32_t accepted=0x71;memcpy(code+1,&accepted,4);
    code[5]=0xe9;const int32_t exit=0x31a85e-(0x31a746+10);memcpy(code+6,&exit,4);
    *Native<uint8_t*>(0x1434c50)=0xc3;
    std::array<uint8_t,0x5664> game{};std::array<uint8_t,0x540> client{};
    uint32_t flags=1u<<20;memcpy(game.data()+0x5660,&flags,4);
    std::vector<uint8_t> save(0x14f);
    const auto put=[&](unsigned offset,uint32_t value){memcpy(save.data()+offset,&value,4);};
    put(0,0xaa55aa55);put(4,98);put(8,unsigned(save.size()));save[36]=0x21;
    const auto checksum=[&]{
        put(12,0);uint32_t sum=0;
        for(auto byte:save)sum=(sum<<1)+(sum>>31)+byte;
        put(12,sum);
    };
    const auto check=[&]{
        auto cursor=save.data();void* player=reinterpret_cast<void*>(1);
        auto result=Native<int(*)(void*,void*,uint8_t**,const uint8_t*,void**)>(0x31a580)(game.data(),client.data(),&cursor,save.data()+save.size(),&player);
        assert(!player);return result;
    };
    if(!capturedSave.empty()){
        save=capturedSave;
        assert(save.size()>=0x14f && save[40]==7 && (save[36]&0x20));
        assert(check()==(extended?int(accepted):9));
        save.assign(0x14f,0);put(0,0xaa55aa55);put(4,98);put(8,unsigned(save.size()));save[36]=0x21;
        std::cout<<"Captured Warlock save reproduces "<<(extended?"successful header validation":"SYSERROR_EXPANSIONGAME")<<".\n";
    }
    for(uint8_t id=0;id<9;++id){
        save[40]=id;checksum();
        const auto expected=id<(extended?8:7)?int(accepted):9;
        assert(check()==expected);
    }
    save[40]=255;checksum();assert(check()==9);
    // Keep enforcing the original expansion and checksum requirements.
    save[40]=0;save[36]=1;checksum();assert(check()==9);
    save[36]=0x21;checksum();save[50]^=1;assert(check()==6);save[50]^=1;
    assert(*Native<uint32_t*>(0x158f5a0+9*4)==23);
    memcpy(Native<void*>(0x31a746),saved.data(),saved.size());
    std::cout<<(extended?"Patched":"Original")<<" native D2R save header validation checked (including the Warlock rejection).\n";
}
void AudioProfileTest(){
    // Exercise the actual unchecked audio table lookup that used to fetch
    // null for class 7, before the crashing dereference at 10a2f7.
    auto code=Native<uint8_t*>(0x1ff2000);
    const uint8_t enter[]{0x89,0xc8,0x48,0xba};memcpy(code,enter,sizeof(enter));
    const uintptr_t target=Pattern::Address(0x10a2ec);memcpy(code+4,&target,8);code[12]=0xff;code[13]=0xe2;
    const auto saved=*Native<uint8_t*>(0x10a2f7);*Native<uint8_t*>(0x10a2f7)=0xc3;
    auto lookup=reinterpret_cast<uintptr_t(*)(unsigned)>(code);
    auto original=Native<uintptr_t*>(0x14b7578);
    assert(!original[7]);
    for(unsigned id=0;id<7;++id)assert(lookup(id)==original[id]);
    assert(lookup(7) && lookup(7)==original[2]);
    *Native<uint8_t*>(0x10a2f7)=saved;
    std::cout<<"Native audio lookup preserves seven profiles and gives Warlock a non-null foley profile.\n";
}
void PresentationTest(){
    auto code=Native<uint8_t*>(0x1ff2100);
    // Preserve the nonvolatile registers used by the real array instructions;
    // the fixture's EDX supplies the actual class ID, R14 the panel object.
    const uint8_t begin[]{0x57,0x41,0x55,0x41,0x56,0x49,0x89,0xce,0x89,0xd0,0x49,0x89,0xc5,0x49,0xbb};
    memcpy(code,begin,sizeof(begin));code[23]=0x41;code[24]=0xff;code[25]=0xe3;
    const std::array<uint8_t,11> leave{0xb8,0,0,0,0,0x41,0x5e,0x41,0x5d,0x5f,0xc3};
    std::array<uint8_t,0x800> panel{};
    for(const auto& site:PresentationBranches){
        const uintptr_t target=Pattern::Address(site.rva);memcpy(code+sizeof(begin),&target,8);
        std::array<uint8_t,sizeof(leave)> originalSkip{},originalContinue{};
        memcpy(originalSkip.data(),Native<void*>(site.skip),originalSkip.size());
        memcpy(originalContinue.data(),Native<void*>(site.rva+site.size),originalContinue.size());
        auto skipped=leave;auto accepted=leave;skipped[1]=0x71;accepted[1]=0x72;
        memcpy(Native<void*>(site.skip),skipped.data(),skipped.size());
        memcpy(Native<void*>(site.rva+site.size),accepted.data(),accepted.size());
        auto invoke=reinterpret_cast<int(*)(void*,unsigned)>(code);
        for(unsigned id=0;id<7;++id)assert(invoke(panel.data(),id)==0x72);
        assert(invoke(nullptr,7)==0x71 && invoke(nullptr,8)==0x71 && invoke(nullptr,255)==0x71);
        memcpy(Native<void*>(site.skip),originalSkip.data(),originalSkip.size());
        memcpy(Native<void*>(site.rva+site.size),originalContinue.data(),originalContinue.size());
    }
    std::cout<<"Native skill-panel label reads preserve seven classes and skip unavailable Warlock labels safely.\n";
}
void AnimationResidencyTest(bool extended){
    auto code=Native<uint8_t*>(0x1ff2300);
    const uint8_t enter[]{0x56,0x57,0x41,0x54,0x41,0x89,0xc9,0x89,0xd6,0x49,0xbb};
    memcpy(code,enter,sizeof(enter));const uintptr_t target=Pattern::Address(0xc0e74);
    memcpy(code+sizeof(enter),&target,8);code[19]=0x41;code[20]=0xff;code[21]=0xe3;
    std::array<uint8_t,5> savedA{},savedB{},savedC{};
    memcpy(savedA.data(),Native<void*>(0xc0e7b),5);memcpy(savedB.data(),Native<void*>(0xc0f08),5);memcpy(savedC.data(),Native<void*>(0xc0f9c),5);
    const auto branch=[](uint32_t at,uint32_t to){auto p=Native<uint8_t*>(at);*p=0xe9;const int32_t offset=to-at-5;memcpy(p+1,&offset,4);};
    branch(0xc0e7b,0xc0ef8);branch(0xc0f08,0xc0f94);
    const uint8_t leave[]{0x41,0x5c,0x5f,0x5e,0xc3};memcpy(Native<void*>(0xc0f9c),leave,5);
    auto flags=extended?reinterpret_cast<uint32_t*>(storage+0x1000):Native<uint32_t*>(0x1d590c0);
    std::array<uint8_t,8*40*4> original{};
    if(extended)memset(flags,0,original.size());else memcpy(original.data(),flags,original.size());
    const auto oldHead=*Native<uintptr_t*>(0x1d59538);
    *Native<uintptr_t*>(0x1d59538)=0x12345678abcdef00ULL;
    auto set=reinterpret_cast<void(*)(unsigned,unsigned)>(code);
    // Execute the native base load, index arithmetic and write for every cell,
    // including class 7 / mode 6: the exact write caught by the watchpoint.
    if(extended){
        for(unsigned id=0;id<8;++id)for(unsigned mode=0;mode<40;++mode)set(id,mode);
        for(unsigned i=0;i<8*40;++i)assert(flags[i]==1);
        assert(*Native<uintptr_t*>(0x1d59538)==0x12345678abcdef00ULL);
        assert(*Native<uint32_t*>(0xc10f4)==8*40*4);
    }else{
        set(7,6);assert(*Native<uintptr_t*>(0x1d59538)==0x1234567800000001ULL);
        memcpy(flags,original.data(),original.size());
    }
    *Native<uintptr_t*>(0x1d59538)=oldHead;
    memcpy(Native<void*>(0xc0e7b),savedA.data(),5);memcpy(Native<void*>(0xc0f08),savedB.data(),5);memcpy(Native<void*>(0xc0f9c),savedC.data(),5);
    std::cout<<(extended?"Native residency writes fit all eight classes without changing the adjacent animation-cache head.":"Original native class-7 residency write reproduces animation-cache corruption.")<<"\n";
}
unsigned allocations=0;
void* __fastcall PlayerAllocation(int type){assert(type==0);++allocations;return nullptr;}
void FrontendTest(){
    auto code=Native<uint8_t*>(0x1ff1000);
    // Execute the native frontend empty-class comparison, using its actual
    // r12+0x24 layout. Class 7 must follow the model path; 8 stays empty.
    const uint8_t enter[]{0x41,0x54,0x49,0x89,0xcc};memcpy(code,enter,sizeof(enter));
    memcpy(code+sizeof(enter),Native<void*>(0x460736),6);
    const uint8_t finish[]{0x0f,0x94,0xc0,0x41,0x5c,0xc3};memcpy(code+sizeof(enter)+6,finish,sizeof(finish));
    std::array<uint32_t,10> preview{};
    for(unsigned i=0;i<9;++i){preview[9]=i;assert(bool(reinterpret_cast<uint8_t(*)(void*)>(code)(preview.data()))==(i==8));}
    // Execute the default-class store in the real widget constructor.
    const uint8_t begin[]{0x56,0x48,0x89,0xce};memcpy(code,begin,sizeof(begin));
    memcpy(code+4,Native<void*>(0xc253c3),10);code[14]=0x5e;code[15]=0xc3;
    std::array<uint8_t,0x100> widget{};reinterpret_cast<void(*)(void*)>(code)(widget.data());
    uint32_t empty=0;memcpy(&empty,widget.data()+0x88,4);assert(empty==8);
    assert(*reinterpret_cast<const uint32_t*>(storage+0x300+7*4)==27566);
    assert(*reinterpret_cast<const uint32_t*>(storage+0x300+8*4)==5383);
    for(const auto& r:Redirects)assert(int64_t(Pattern::Address(r.next))+*Native<int32_t*>(r.displacement)==int64_t(reinterpret_cast<uintptr_t>(storage+r.offset)));
    // Run the actual generic player creation guard. A null allocator avoids
    // requiring a game world while still proving that valid classes reach it.
    Jump(0x208670,PlayerAllocation);
    auto create=Native<void*(__fastcall*)(int,int,int,int,void*,void*,int,int,int)>(0x2c42a0);
    for(int i=0;i<8;++i){auto before=allocations;assert(!create(0,i,0,0,nullptr,nullptr,0,0,0));assert(allocations==before+1);}
    auto before=allocations;assert(!create(0,8,0,0,nullptr,nullptr,0,0,0) && allocations==before);
    assert(!create(0,-1,0,0,nullptr,nullptr,0,0,0) && allocations==before);
    std::cout<<"Native frontend accepts Warlock, preserves empty previews, redirects class names and permits player allocation.\n";
}
void SkillsTest(){
    Jump(0x78c60,CountsResize);Jump(0x1dfe90,IndicesResize);Jump(0x1438100,memset);
    std::array<std::array<uint8_t,0x284>,6> skills{};
    const uint8_t classes[]{0,6,7,7,0xff,8};
    for(unsigned i=0;i<skills.size();++i)skills[i][0x2c]=classes[i];
    *Native<uintptr_t*>(0x1c9fad0)=reinterpret_cast<uintptr_t>(skills.data());
    *Native<uint64_t*>(0x1c9fad8)=skills.size();
    skillCounts.fill(0);skillCounts.front()=0x12345678;skillCounts.back()=0xabcdef;
    // Run the real allocation/counting code, including its memset byte counts.
    std::array<uint8_t,17> continuation{};memcpy(continuation.data(),Native<void*>(0x1da899),continuation.size());
    reinterpret_cast<void(*)()>(RegisterThunk(0x1da7c7,0x1da899))();
    assert(*Native<uint64_t*>(0x1c9faf8)==8);
    assert(skillCounts[1]==1 && skillCounts[7]==1 && skillCounts[8]==2);
    assert(skillCounts.front()==0x12345678 && skillCounts.back()==0xabcdef);
    memcpy(Native<void*>(0x1da899),continuation.data(),continuation.size());
    // Continue the actual max-count/index-vector allocation and reset block.
    reinterpret_cast<void(*)()>(RegisterThunk(0x1da899,0x1da97a))();
    assert(*Native<int32_t*>(0x1c9fb08)==2);
    assert(*Native<uint64_t*>(0x1c9fb18)==16);
    assert(skillIndices.front()==0xfefe && skillIndices.back()==0xfefe);
    assert(skillCounts.front()==0x12345678 && skillCounts.back()==0xabcdef);
    for(unsigned i=1;i<9;++i)assert(skillCounts[i]==0);
    std::cout<<"Native skill cache allocates eight counts and eight index slices without overwriting guards.\n";
}
int wmain(int argc,wchar_t** argv){
    assert(argc==2 || argc==3);
    if(argc==3){
        std::ifstream save(std::filesystem::path(argv[2]),std::ios::binary|std::ios::ate);assert(save);
        capturedSave.resize(size_t(save.tellg()));save.seekg(0);save.read(reinterpret_cast<char*>(capturedSave.data()),capturedSave.size());assert(save);
    }
    std::ifstream file(std::filesystem::path(argv[1]),std::ios::binary|std::ios::ate);assert(file);
    std::vector<uint8_t> bytes(size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(file);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data()+dos->e_lfanew);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);testImage=reinterpret_cast<uintptr_t>(image);
    auto sections=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)memcpy(image+sections[i].VirtualAddress,bytes.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    assert(Supported());image[0x28fde7]^=1;assert(!Supported());image[0x28fde7]^=1;
    SaveHeaderTest(false);
    SaveSkillLengthTest(false);
    AnimationResidencyTest(false);
    // Windows normally relocates these native pointers. This fixture maps
    // sections privately, so relocate only the audited audio profile table.
    for(unsigned i=0;i<7;++i){auto p=Native<uintptr_t*>(0x14b7578)+i;*p=testImage+(*p-nt->OptionalHeader.ImageBase);}
    assert(PrepareStorage() && ApplyPatches(true));
    for(const auto& p:runtimePatches)assert(!memcmp(image+p.rva,p.after.data(),p.size));
    SaveHeaderTest(true);SaveSkillLengthTest(true);AudioProfileTest();PresentationTest();AnimationResidencyTest(true);InventoryTest();FrontendTest();RegistryTest();CreationInfoTest();SkillsTest();
    assert(std::string(ModelHook(nullptr,0,7))=="warlock");
    assert(ApplyPatches(false));
    for(const auto& p:runtimePatches)assert(!memcmp(image+p.rva,p.before.data(),p.size));
    for(const auto& r:Redirects)assert(*Native<int32_t*>(r.displacement)==int32_t(r.target-r.next));
    VirtualFree(storage,0,MEM_RELEASE);VirtualFree(image,0,MEM_RELEASE);
    std::cout<<"Class patch signature rejection and restoration passed.\n";
}
