#include "HitchDiagnostics.h"
#include "RuntimeTelemetry.h"
#include "ProcessLifetime.h"
#include "json.hpp"
#include <psapi.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <sstream>
#include <cmath>
#pragma comment(lib,"psapi.lib")

namespace HudHitches {
namespace {
using Json=nlohmann::json;
std::atomic<bool> recording{false};std::atomic<uint64_t> generation{0},context{255},session{0},dropped{0};
std::atomic<bool> cadenceLost{false};
int64_t Clock(){LARGE_INTEGER t{};QueryPerformanceCounter(&t);return t.QuadPart;}
uint64_t FileTicks(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
uint64_t ThreadCpu(){FILETIME a{},b{},k{},u{};return GetThreadTimes(GetCurrentThread(),&a,&b,&k,&u)?FileTicks(k)+FileTicks(u):0;}
struct Span {HudTelemetry::Feature feature{};int64_t start=0,end=0;uint32_t thread=0;};
struct Usage {int64_t at=0;uint64_t processCpu=0,read=0,write=0,working=0,privateBytes=0,faults=0,available=0,systemBusy=0,systemTotal=0;bool cpuValid=false,ioValid=false,memoryValid=false,systemValid=false;};
Usage Sample(){
    Usage r;r.at=Clock();FILETIME a{},b{},k{},u{},idle{};
    r.cpuValid=GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u)!=0;r.processCpu=FileTicks(k)+FileTicks(u);
    IO_COUNTERS io{};r.ioValid=GetProcessIoCounters(GetCurrentProcess(),&io)!=0;r.read=io.ReadTransferCount;r.write=io.WriteTransferCount;
    PROCESS_MEMORY_COUNTERS_EX mem{};mem.cb=sizeof(mem);r.memoryValid=GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mem),sizeof(mem))!=0;
    r.working=mem.WorkingSetSize;r.privateBytes=mem.PrivateUsage;r.faults=mem.PageFaultCount;
    MEMORYSTATUSEX global{};global.dwLength=sizeof(global);if(GlobalMemoryStatusEx(&global))r.available=global.ullAvailPhys;
    r.systemValid=GetSystemTimes(&idle,&k,&u)!=0;r.systemTotal=FileTicks(k)+FileTicks(u);r.systemBusy=r.systemTotal-FileTicks(idle);return r;
}
struct Capture {
    Options options;int64_t start=0,end=0,frequency=1;uint64_t generation=0;uintptr_t chain=0;unsigned processors=1;
    std::filesystem::path folder;std::string identity,startedUtc;
    Ring<Span> spans{65536};Ring<Frame> frames{36000};Ring<Usage> usage{2401};Summary summary;std::vector<Frame> worst;
    bool previous=false,previousFocus=false;int64_t previousAt=0;uint64_t previousCpu=0,previousContext=255,previousSession=0;
    uint32_t previousThread=0;
    double previousHud=0,previousPresent=0;
    double Ms(int64_t ticks)const{return double(ticks)*1000./double(frequency);}
};
struct Runtime {std::mutex mutex;std::shared_ptr<Capture> capture;bool saving=false;std::string status;std::filesystem::path report;};
Runtime& State(){return HudLifetime::State<Runtime>();}
void Trace(uint64_t gen,HudTelemetry::Feature feature,int64_t start,int64_t end,uint32_t thread) noexcept {
    if(!recording.load(std::memory_order_relaxed)||generation.load(std::memory_order_relaxed)!=gen)return;
    auto& s=State();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++dropped;return;}
    auto c=s.capture;if(!recording.load()||!c||c->generation!=gen||start<c->start||end<start)return;
    // Always retain the HUD frame; trim tiny high-frequency filter/hook calls.
    if(feature!=HudTelemetry::Feature::Frame&&end-start<c->frequency/10000)return;
    c->spans.Add({feature,start,end,thread});
}
std::string Phase(const Frame& f){
    if(f.presentMs>=f.intervalMs*.5)return "Present/driver wait";
    if(f.hudMs>=f.intervalMs*.5)return "HUD overlay work/wait";
    return "Outside HUD/Present (game, scheduling or unmeasured work)";
}
Json FrameJson(const Frame& f,const Capture& c){return {{"at_ms",c.Ms(f.at-c.start)},{"frame_ms",f.intervalMs},{"previous_hud_overlay_ms",f.hudMs},{"previous_present_ms",f.presentMs},{"outside_hud_present_ms",(std::max)(0.,f.intervalMs-f.hudMs-f.presentMs)},{"render_thread_cpu_ms",f.threadCpuMs},{"gameplay",f.eligible},{"sample_gap",f.sampleGap},{"status",f.context&255},{"loading",bool(f.context&256)},{"hud_menu",bool(f.context&512)},{"marketplace",bool(f.context&1024)},{"stash_page",(f.context>>16)&255},{"session",f.session},{"largest_measured_phase",Phase(f)}};}
Json SummaryJson(const Capture& c){const auto& s=c.summary;return {{"gameplay_frames",s.frames},{"hitches",s.hitches},{"excluded_frames",s.excluded},{"recorded_frames",s.observed},{"all_context_pauses",s.allHitches},{"all_context_worst_ms",s.allMaximumMs},{"mean_ms",s.Mean()},{"worst_ms",s.maximumMs},{"p95_upper_ms",s.P95Upper()},{"threshold_ms",c.options.thresholdMs},{"duration_seconds",c.Ms(c.end-c.start)/1000.},{"dropped_on_contention",dropped.load()},{"overwritten_frames",c.frames.Overwritten()},{"overwritten_spans",c.spans.Overwritten()}};}
std::filesystem::path Export(const Capture& c){
    Json out={{"schema",1},{"tool","D2RHUD hitch recorder"},{"started_utc",c.startedUtc},{"identity",c.identity},{"summary",SummaryJson(c)},
        {"notes",Json::array({"Frame intervals are CPU-observed Present-entry cadence, not GPU execution time or network latency.","Each interval contains the PREVIOUS Present call's HUD overlay and driver wait. Nested feature spans overlap; do not add them together.","Loading, unfocused, HUD-menu, session-transition and collector sample-gap intervals are excluded from gameplay hitch counts but retained in frames.","Process I/O includes cached/file/device traffic; correlation alone does not prove disk or network stalls. Page-fault counts include soft faults.","Tiny feature spans below 0.1 ms are omitted. Buffers are bounded; overwritten counts and retained windows describe missing history.","No character saves, item bytes, chat, credentials, process inventory or remote upload are included."})},
        {"frames",Json::array()},{"worst_hitches",Json::array()},{"resources",Json::array()},{"traceEvents",Json::array()}};
    auto& trace=out["traceEvents"];trace.push_back({{"ph","M"},{"pid",1},{"tid",0},{"name","thread_name"},{"args",{{"name","Frame cadence (wall)"}}}});
    for(size_t i=0;i<c.frames.Size();++i){const auto& f=c.frames.At(i);out["frames"].push_back(FrameJson(f,c));if(f.intervalMs>0)trace.push_back({{"ph","X"},{"pid",1},{"tid",0},{"cat",f.eligible?"gameplay":"excluded"},{"name",f.eligible&&f.intervalMs>=c.options.thresholdMs?"Hitch":"Frame"},{"ts",(c.Ms(f.at-c.start)-f.intervalMs)*1000.},{"dur",f.intervalMs*1000.},{"args",{{"HUD overlay ms",f.hudMs},{"Present ms",f.presentMs},{"render CPU ms",f.threadCpuMs}}}});}
    for(const auto& f:c.worst)out["worst_hitches"].push_back(FrameJson(f,c));
    int64_t firstSpan=c.end;
    for(size_t i=0;i<c.spans.Size();++i){const auto& e=c.spans.At(i);firstSpan=(std::min)(firstSpan,e.start);trace.push_back({{"ph","X"},{"pid",1},{"tid",e.thread},{"cat","HUD feature (inclusive wall time)"},{"name",HudTelemetry::Names[size_t(e.feature)]},{"ts",c.Ms(e.start-c.start)*1000.},{"dur",c.Ms(e.end-e.start)*1000.}});}
    out["retained"]={{"frames_from_ms",c.frames.Size()?c.Ms(c.frames.At(0).at-c.start):0},{"spans_from_ms",c.spans.Size()?c.Ms(firstSpan-c.start):0},{"resources_from_ms",c.usage.Size()?c.Ms(c.usage.At(0).at-c.start):0}};
    for(size_t i=0;i<c.usage.Size();++i){const auto& r=c.usage.At(i);Json row={{"at_ms",c.Ms(r.at-c.start)},{"working_set_bytes",r.working},{"private_bytes",r.privateBytes},{"available_physical_bytes",r.available},{"memory_valid",r.memoryValid},{"cpu_valid",r.cpuValid},{"io_valid",r.ioValid},{"system_valid",r.systemValid}};
        if(i){const auto& p=c.usage.At(i-1);const auto seconds=c.Ms(r.at-p.at)/1000.;if(seconds>0){
            if(r.cpuValid&&p.cpuValid&&r.processCpu>=p.processCpu){row["process_cpu_one_core_percent"]=double(r.processCpu-p.processCpu)/1e7/seconds*100.;row["process_cpu_machine_percent"]=row["process_cpu_one_core_percent"].get<double>()/c.processors;}
            if(r.ioValid&&p.ioValid&&r.read>=p.read&&r.write>=p.write){row["read_bytes"]=r.read-p.read;row["write_bytes"]=r.write-p.write;}
            if(r.memoryValid&&p.memoryValid&&r.faults>=p.faults)row["page_faults"]=r.faults-p.faults;
            if(r.systemValid&&p.systemValid&&r.systemTotal>p.systemTotal&&r.systemBusy>=p.systemBusy)row["system_cpu_percent"]=100.*double(r.systemBusy-p.systemBusy)/double(r.systemTotal-p.systemTotal);
        }}out["resources"].push_back(row);
        Json counters={{"Working set MiB",double(r.working)/1048576.},{"Private MiB",double(r.privateBytes)/1048576.}};
        if(row.contains("process_cpu_machine_percent"))counters["Process CPU %"]=row["process_cpu_machine_percent"];
        if(row.contains("system_cpu_percent"))counters["System CPU %"]=row["system_cpu_percent"];
        trace.push_back({{"ph","C"},{"pid",1},{"tid",0},{"name","Process resources"},{"ts",c.Ms(r.at-c.start)*1000.},{"args",counters}});
    }
    std::filesystem::create_directories(c.folder);SYSTEMTIME t{};GetSystemTime(&t);wchar_t name[128]{};
    swprintf_s(name,L"hitches-%04u%02u%02u-%02u%02u%02u-%lu-%llu.json",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),c.generation);
    const auto path=c.folder/name,temp=std::filesystem::path(path.wstring()+L".tmp");
    try{std::ofstream file(temp,std::ios::binary);file<<out.dump(1);file.flush();if(!file)throw std::runtime_error("Could not write hitch report.");file.close();if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not finalize hitch report.");}
    catch(...){std::error_code error;std::filesystem::remove(temp,error);throw;}return path;
}
void StopGeneration(uint64_t gen){auto& s=State();std::lock_guard lock(s.mutex);if(generation.load()!=gen||!recording.load())return;recording=false;HudTelemetry::SetTraceSink(nullptr);s.capture->end=Clock();s.saving=true;s.status="Saving hitch report in the background...";}
void Worker(std::shared_ptr<Capture> c) noexcept {
    try{
        while(recording.load()&&generation.load()==c->generation){
            const auto usage=Sample();{auto& s=State();std::lock_guard lock(s.mutex);if(recording.load()&&generation.load()==c->generation)c->usage.Add(usage);}
            if(c->Ms(Clock()-c->start)>=c->options.seconds*1000.){StopGeneration(c->generation);break;}Sleep(250);
        }
        // StopGeneration serializes all producers before this read-only export.
        const auto report=Export(*c);auto& s=State();std::lock_guard lock(s.mutex);s.report=report;s.status="Hitch report saved. Send this JSON file for analysis.";s.saving=false;
    }catch(const std::exception& e){StopGeneration(c->generation);auto& s=State();std::lock_guard lock(s.mutex);s.status=std::string("Hitch recording/report failed: ")+e.what();s.saving=false;}
    catch(...){StopGeneration(c->generation);auto& s=State();std::lock_guard lock(s.mutex);s.status="Hitch recording/report failed.";s.saving=false;}
}
}
bool Active() noexcept{return recording.load(std::memory_order_relaxed);}
bool Start(Options options,const std::filesystem::path& folder,std::string identity){
    if(!std::isfinite(options.thresholdMs)||options.thresholdMs<16||options.thresholdMs>1000||options.seconds<1||options.seconds>600||folder.empty())return false;
    auto& s=State();std::lock_guard lock(s.mutex);if(recording.load()||s.saving)return false;
    try{auto c=std::make_shared<Capture>();c->options=options;c->folder=folder;c->identity=std::move(identity);c->start=Clock();LARGE_INTEGER freq{};if(!QueryPerformanceFrequency(&freq)||freq.QuadPart<=0)return false;c->frequency=freq.QuadPart;
        SYSTEM_INFO system{};GetSystemInfo(&system);c->processors=(std::max)(1u,unsigned(system.dwNumberOfProcessors));c->worst.reserve(32);
        SYSTEMTIME t{};GetSystemTime(&t);char utc[64]{};sprintf_s(utc,"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);c->startedUtc=utc;
        c->end=c->start;c->generation=HudTelemetry::SetTraceSink(Trace);generation=c->generation;dropped=0;cadenceLost=false;context=255;session=0;s.capture=c;s.report.clear();s.status="Recording. Close this menu and reproduce the hitch; the report saves automatically.";recording=true;
        try{std::thread([c]{Worker(c);}).detach();}catch(...){recording=false;HudTelemetry::SetTraceSink(nullptr);throw;}return true;
    }catch(const std::exception& e){s.status=e.what();return false;}
}
void Stop(){StopGeneration(generation.load());}
void SetContext(int status,bool loading,bool menu,bool marketplace,unsigned page,uint64_t epoch) noexcept {
    if(!Active())return;context=uint64_t(status&255)|(loading?256:0)|(menu?512:0)|(marketplace?1024:0)|(uint64_t(page&255)<<16);session=epoch;
}
View Snapshot(){
    View v;auto& s=State();std::lock_guard lock(s.mutex);v.recording=recording.load();v.saving=s.saving;v.status=s.status;v.report=s.report;
    if(auto c=s.capture){v.start=c->start;v.frequency=c->frequency;v.options=c->options;v.summary=c->summary;v.elapsed=c->Ms((v.recording?Clock():c->end)-c->start)/1000.;v.remaining=(std::max)(0.,double(c->options.seconds)-v.elapsed);v.dropped=dropped.load();v.overwritten=c->frames.Overwritten()+c->spans.Overwritten();v.worst=c->worst;
        const auto count=(std::min)(size_t(240),c->frames.Size());v.graphCount=int(count);for(size_t i=0;i<count;++i)v.graph[i]=float(c->frames.At(c->frames.Size()-count+i).intervalMs);
    }return v;
}
Present::Present(void* chain,bool test,void* window) noexcept {
    if(test||!Active())return;generation_=generation.load();chain_=reinterpret_cast<uintptr_t>(chain);start_=Clock();cpu_=ThreadCpu();thread_=GetCurrentThreadId();focused_=window&&GetForegroundWindow()==window&&!IsIconic(static_cast<HWND>(window));
}
void Present::BeforeDriver() noexcept {if(generation_)hudEnd_=Clock();}
Present::~Present(){
    if(!generation_||!Active()||generation.load()!=generation_)return;const auto end=Clock();auto& s=State();std::unique_lock lock(s.mutex,std::try_to_lock);if(!lock){++dropped;cadenceLost=true;return;}
    const auto c=s.capture;if(!recording.load()||!c||c->generation!=generation_)return;if(c->chain&&c->chain!=chain_)return;c->chain=chain_;
    if(c->previous&&start_<=c->previousAt){++dropped;cadenceLost=true;return;}
    const auto ctx=context.load(),epoch=session.load();const bool lost=cadenceLost.exchange(false);
    if(c->previous&&start_>c->previousAt){Frame f{start_,c->Ms(start_-c->previousAt),c->previousHud,c->previousPresent,cpu_&&c->previousCpu&&cpu_>=c->previousCpu&&thread_==c->previousThread?double(cpu_-c->previousCpu)/10000.:0,ctx,epoch,!lost&&thread_==c->previousThread&&Eligible(c->previousContext,ctx,c->previousSession,epoch,c->previousFocus,focused_),lost};
        c->frames.Add(f);c->summary.Add(f,c->options.thresholdMs);
        if(f.eligible&&f.intervalMs>=c->options.thresholdMs){auto& rows=c->worst;if(rows.size()<32)rows.push_back(f);else if(f.intervalMs>rows.back().intervalMs)rows.back()=f;std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.intervalMs>b.intervalMs;});}
    }
    c->previous=true;c->previousAt=start_;c->previousCpu=cpu_;c->previousThread=thread_;c->previousContext=ctx;c->previousSession=epoch;c->previousFocus=focused_;c->previousHud=c->Ms((hudEnd_?hudEnd_:end)-start_);c->previousPresent=hudEnd_?c->Ms(end-hudEnd_):0;
}
}
