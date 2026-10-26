// The native draw function receives each exploration tree by argument. Draw
// our private trees alongside it; never swap the global current-layer pointer.
using MapCells=void(__fastcall*)(int,void*,uint8_t*,void*);
using MapReset=void(__fastcall*)();
MapCells originalMapCells=nullptr;MapReset originalMapReset=nullptr;
std::array<AutomapData::Assembly,2> automapStreams;
std::shared_ptr<AutomapData::Geometry> eventGeometry;
const std::vector<AutomapData::Entry>* geometrySource=nullptr;
uint64_t lastAutomapRequest=0;
void ClearEventAutomap(){automapStreams={};eventGeometry.reset();geometrySource=nullptr;lastAutomapRequest=0;}
bool ReceiveAutomap(const AutomapData::Wire& packet){
    if(!AutomapData::Valid(packet))return false;
    std::lock_guard lock(viewMutex);
    if(!receivedAt || !AutomapData::Visible(view,GetTickCount64()-receivedAt,view.recipient,view.level) || !AutomapData::SameEvent(packet,view))return false;
    auto& stream=automapStreams[size_t(packet.stream)];if(!stream.Accept(packet))return false;
    if(packet.stream==AutomapData::Stream::Geometry && stream.complete && stream.ready.get()!=geometrySource){
        eventGeometry=std::make_shared<AutomapData::Geometry>(*stream.ready);geometrySource=stream.ready.get();
    }return true;
}
void __fastcall MapResetHook(){
    // Native reset is called from complete client-game/UI teardown. Prevent a reused player ID
    // and cow-level ID in another game from matching a stale event packet.
    Survivors::SnapshotBody saved;
    {std::lock_guard lock(viewMutex);saved=survivorView;ClearEventAutomap();view={};receivedAt=0;survivorView={};survivorReceived=0;}
    SaveSurvivorProfile(saved,true);
    originalMapReset();
    // Keep plans through the native save above, then discard them before the
    // next game can generate levels using the same offline world seed.
    ClearClientLayouts();
    ClearClientRewards();
    ClearMonsterReveal();
}
void DrawEventMarkers(const std::vector<AutomapData::Entry>& markers,const uint8_t* transform){
    const float scale=Field<float>(transform,0x38);
    const int left=Field<int>(transform,0x18),top=Field<int>(transform,0x1c);
    const int width=Field<int>(transform,0x20),height=Field<int>(transform,0x24);
    if(!std::isfinite(scale) || scale<=0 || width<=0 || height<=0)return;
    for(const auto& marker:markers){
        const auto point=AutomapData::Project({marker.x,marker.y},transform);
        if(point.x<left || point.y<top || int64_t(point.x)>=int64_t(left)+width || int64_t(point.y)>=int64_t(top)+height)continue;
        const uint64_t packed=uint32_t(point.x)|(uint64_t(uint32_t(point.y))<<32);
        // Native units.sprite resource, including its native size and clipping.
        Native<void(__fastcall*)(uint64_t,int,float)>(0x977a0)(packed,AutomapData::SpriteFrame(marker.kind),scale);
    }
}
void __fastcall MapCellsHook(int style,void* tree,uint8_t* transform,void* opacity){
    Packet packet;uint64_t time=0;std::shared_ptr<AutomapData::Geometry> geometry;
    std::shared_ptr<const std::vector<AutomapData::Entry>> monsters;
    {std::lock_guard lock(viewMutex);packet=view;time=receivedAt;geometry=eventGeometry;monsters=automapStreams[1].ready;}
    auto player=LocalPlayer();
    if(!installed.load() || !player){originalMapCells(style,tree,transform,opacity);return;}
    const auto age=GetTickCount64()-time;
    const bool eventVisible=time && AutomapData::Visible(packet,age,player->dwUnitId,Level(player));
    auto reveal=VisibleMonsterReveal(player,packet,time?age:UINT64_MAX);
    if(!eventVisible && !reveal){originalMapCells(style,tree,transform,opacity);return;}
    const auto layer=Field<uint8_t*>(Native<void*>(0x1d56150),0);
    if(!layer || !transform){originalMapCells(style,tree,transform,opacity);return;}
    if(eventVisible && !geometry && packet.eventPhase==EventPhase::Active){
        bool request=false;const auto now=GetTickCount64();
        {std::lock_guard lock(viewMutex);if(!lastAutomapRequest || now-lastAutomapRequest>=1000){lastAutomapRequest=now;request=true;}}
        if(request){CommandPacket command;command.run=packet.run;command.session=packet.session;command.phase=EventPhase::Active;
            Native<void(__fastcall*)(void*,int)>(0x10dce0)(&command,sizeof(command));}
    }
    void* visible=tree;
    const bool validImages=eventVisible && geometry && geometry->highestImage<Field<int>(Native<void*>(0x1c9b89c),0) && Field<int>(Native<void*>(0x1c9b898),0)>0;
    if(validImages && geometry->trees[0].count && tree==layer+8)visible=&geometry->trees[0];
    if(validImages && geometry->trees[1].count && tree==layer+0x30)visible=&geometry->trees[1];
    originalMapCells(style,visible,transform,opacity);
    if(tree==layer+0x58){
        if(eventVisible && monsters)DrawEventMarkers(*monsters,transform);
        else if(reveal)DrawEventMarkers(*reveal,transform);
    }
}
