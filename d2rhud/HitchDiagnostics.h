#pragma once
#include "HitchModel.h"
#include <filesystem>
#include <string>

namespace HudHitches {
struct Options {double thresholdMs=50;unsigned seconds=300;};
struct View {
    bool recording=false,saving=false;double elapsed=0,remaining=0;Options options;Summary summary;
    int64_t start=0,frequency=1;
    uint64_t dropped=0,overwritten=0;std::array<float,240> graph{};int graphCount=0;
    std::vector<Frame> worst;std::string status;std::filesystem::path report;
};
bool Start(Options options,const std::filesystem::path& folder,std::string identity);
void Stop();
bool Active() noexcept;
View Snapshot();
void SetContext(int status,bool loading,bool menu,bool marketplace,unsigned page,uint64_t session) noexcept;
// No clocks, process queries or buffers are touched by disabled presentation.
class Present {
    uint64_t generation_=0;uintptr_t chain_=0;int64_t start_=0,hudEnd_=0;uint64_t cpu_=0;uint32_t thread_=0;bool focused_=false;
public:
    Present(void* chain,bool test,void* window) noexcept;
    void BeforeDriver() noexcept;
    ~Present();
    Present(const Present&)=delete;
};
}
