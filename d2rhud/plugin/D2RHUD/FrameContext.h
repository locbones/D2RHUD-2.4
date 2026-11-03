#pragma once
#include "../../Pattern.h"
#include <cstdint>

namespace HudFrame {
struct Context {
    int status = -1;
    bool inGame = false, loading = false;
    uint64_t nativeToken = 0;
    uint64_t character = 0;
};
// These flags reside in the verified main image. This snapshot belongs to one
// render update, never to a queued command or a later native memory write.
inline Context Capture() {
    Context value;
    if (const auto address = Pattern::Address(0x1dc76f8)) {
        value.status = *reinterpret_cast<const uint8_t*>(address);
        value.inGame = value.status == 1;
    }
    if (value.inGame) {
        if (const auto address = Pattern::Address(0x1d617d0)) value.nativeToken = *reinterpret_cast<const uint64_t*>(address);
        if (const auto address = Pattern::Address(0x1cb410a)) value.loading = *reinterpret_cast<const uint8_t*>(address) != 0;
        if(const auto selector=Pattern::Address(0x1d442d8)){
            const auto index=*reinterpret_cast<const uint32_t*>(selector);
            if(index<8)if(const auto address=Pattern::Address(0x1c9b200+index*62)){
                const auto name=reinterpret_cast<const unsigned char*>(address);
                if(name[0]){value.character=14695981039346656037ULL;for(size_t i=0;i<16 && name[i];++i)value.character=(value.character^name[i])*1099511628211ULL;}
            }
        }
    }
    return value;
}
}
