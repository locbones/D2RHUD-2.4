#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

namespace Marketplace {
// Latest user selection wins. A short save lease must defer navigation rather
// than discard it; a second request may also arrive during native loading.
// Track actual control changes separately from the in-flight page. A timer
// can advance the page while the control still displays its previous value.
class PageSelection {
    std::mutex mutex;std::optional<uint32_t> observed;
public:
    bool Changed(uint32_t page,uint32_t loaded){
        if(page>=64)return false;std::lock_guard lock(mutex);
        const bool changed=page!=observed.value_or(loaded);observed=page;return changed;
    }
    void Show(uint32_t page){std::lock_guard lock(mutex);observed=page;}
    void Reset(){std::lock_guard lock(mutex);observed.reset();}
};
struct PageRequest {uint32_t page=0;bool carry=false;};
class PageRequests {
    std::mutex mutex;std::optional<PageRequest> next;uint64_t retryAt=0;
public:
    void Request(uint32_t page,bool carry=false){if(page>=64)return;std::lock_guard lock(mutex);next=PageRequest{page,carry};retryAt=0;}
    std::optional<PageRequest> Take(uint32_t loaded,uint64_t now){
        std::lock_guard lock(mutex);if(!next||now<retryAt)return {};
        const auto request=*next;next.reset();if(request.page==loaded)return {};return request;
    }
    void Defer(PageRequest request,uint64_t now){std::lock_guard lock(mutex);if(!next)next=request;retryAt=now+100;}
    void Reset(){std::lock_guard lock(mutex);next.reset();retryAt=0;}
};
}
