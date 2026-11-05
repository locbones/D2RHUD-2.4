#include "../d2rhud/HitchDiagnostics.h"
#include "../d2rhud/RuntimeTelemetry.h"
#include "../d2rhud/json.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>

int main(){
    using namespace HudHitches;
    Ring<int> ring(3);for(int i=0;i<8;++i)ring.Add(i);assert(ring.Size()==3&&ring.Overwritten()==5&&ring.At(0)==5&&ring.At(2)==7);
    assert(Eligible(1,1,5,5,true,true));for(auto flag:{256u,512u})assert(!Eligible(1,1|flag,5,5,true,true));
    assert(!Eligible(1,1,5,6,true,true)&&!Eligible(1,1,5,5,false,true)&&!Eligible(0,1,5,5,true,true));
    Summary summary;for(double ms:{10.,16.,25.,60.,100.}){Frame f;f.eligible=true;f.intervalMs=ms;summary.Add(f,50);}
    Frame ignored;ignored.intervalMs=10000;summary.Add(ignored,50);assert(summary.frames==5&&summary.hitches==2&&summary.excluded==1&&summary.maximumMs==100&&summary.Mean()>42&&summary.P95Upper()==100);
    assert(summary.observed==6&&summary.allHitches==3&&summary.allMaximumMs==10000);ignored.sampleGap=true;summary.Add(ignored,50);assert(summary.allHitches==3); // Menu pauses remain visible; missing samples never fabricate pauses.
    const auto root=std::filesystem::temp_directory_path()/("hud-hitches-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directories(root);
    assert(!Active()&&!Start({50,0},root,"invalid")&&!Start({0,1},root,"invalid"));
    HudTelemetry::SetEnabled(false);HudTelemetry::Reset();
    assert(Start({50,2},root,"Fixture build identity"));assert(!Start({50,2},root,"nested"));
    auto oldScope=std::make_unique<HudTelemetry::Scope>(HudTelemetry::Feature::AssetRead);
    SetContext(1,false,false,false,16,9);auto window=GetForegroundWindow();auto chain=reinterpret_cast<void*>(uintptr_t(1));
    auto frame=[&](unsigned hud,unsigned driver){Present present(chain,false,window);{HudTelemetry::Scope timing(HudTelemetry::Feature::MarketplaceScan);Sleep(hud);}present.BeforeDriver();Sleep(driver);};
    frame(60,5);Sleep(15);frame(1,1); // A real wall-time HUD stall belongs to the previous frame interval.
    std::vector<std::thread> workers;for(int n=0;n<4;++n)workers.emplace_back([]{for(int i=0;i<10;++i){HudTelemetry::Scope timing(HudTelemetry::Feature::AssetRead);Sleep(1);}});for(auto& worker:workers)worker.join();
    SetContext(1,true,false,false,16,9);Sleep(60);frame(1,1);SetContext(1,false,false,false,16,10);frame(1,1); // Loading/session transitions excluded.
    {Present other(reinterpret_cast<void*>(uintptr_t(2)),false,window);other.BeforeDriver();} // Auxiliary swapchain ignored.
    {Present test(chain,true,window);test.BeforeDriver();} // DXGI_PRESENT_TEST ignored.
    Stop();Stop(); // Idempotent; file export stays off the calling thread.
    View view;for(int i=0;i<200;++i){view=Snapshot();if(!view.saving&&!view.report.empty())break;Sleep(25);}
    assert(!Active()&&!view.saving&&!view.report.empty());
    nlohmann::json report;std::ifstream(view.report)>>report;
    assert(report["schema"]==1&&report["identity"]=="Fixture build identity"&&report["frames"].size()==3&&report["resources"].size()>=1);
    assert(report["frames"][0]["previous_hud_overlay_ms"].get<double>()>=50&&report["frames"][0]["frame_ms"].get<double>()>=70);
    assert(report["frames"][1]["gameplay"]==false&&report["frames"][2]["gameplay"]==false);
    if(report["frames"][0]["gameplay"]==true)assert(report["summary"]["hitches"].get<int>()>=1);
    bool found=false;for(const auto& event:report["traceEvents"]){assert(event["ph"]=="M"||event["ts"].is_number());if(event.value("name","")=="Marketplace live item scan")found=true;}
    assert(found&&!HudTelemetry::Enabled()&&HudTelemetry::Snapshot()[size_t(HudTelemetry::Feature::MarketplaceScan)].calls==0);
    const auto first=view.report;
    assert(Start({50,1},root,"Auto-stop fixture"));oldScope.reset();for(int i=0;i<200;++i){view=Snapshot();if(!view.recording&&!view.saving&&!view.report.empty())break;Sleep(25);}assert(!Active()&&view.report!=first&&!view.report.empty());
    std::ifstream(view.report)>>report;assert(report["summary"]["gameplay_frames"]==0&&report["resources"].size()>=3);
    for(const auto& event:report["traceEvents"])assert(event.value("name","")!="Asset file access"); // Old scopes cannot leak into a restarted capture.
    assert(!HudTelemetry::traceSink.load());
    // A failed export is surfaced and does not leave recording permanently busy.
    const auto blocked=root/"not-a-directory";std::ofstream(blocked)<<"sentinel";assert(Start({50,1},blocked,"Failure fixture"));Stop();
    for(int i=0;i<200;++i){view=Snapshot();if(!view.saving&&!view.recording)break;Sleep(25);}assert(view.report.empty()&&view.status.find("failed")!=std::string::npos);
    assert(Start({50,1},root,"Recovery fixture"));Stop();for(int i=0;i<200;++i){view=Snapshot();if(!view.saving&&!view.recording)break;Sleep(25);}assert(!view.report.empty());
    std::cout<<"PASS: bounded chronological trace; gameplay exclusions/percentiles; real timed hitch attribution to previous frame; threaded spans without enabling counters; test/auxiliary swapchain exclusion; asynchronous unique export; auto-stop; file failure and repeat capture. Reports: "<<root<<'\n';
}
