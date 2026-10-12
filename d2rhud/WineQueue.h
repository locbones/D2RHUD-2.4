#pragma once
#include <Windows.h>
#include <array>
#include <cstdint>

// Read-only calibration of D3DMetal's wrapped command-queue pointer. Never call
// COM methods on scan candidates, or assume a private DXGI structure offset.
namespace WineQueue {
    constexpr size_t ScanSlots = 512;
    constexpr uintptr_t MaxAdjustment = 0x1000;
    using Methods = std::array<uintptr_t, 19>;
    struct Sample {
        uintptr_t queue = 0;
        std::array<uintptr_t, ScanSlots> slots{};
        std::array<bool, ScanSlots> readable{};
    };
    struct Layout {
        size_t offset = 0;
        intptr_t adjustment = 0;
        Methods methods{};
        bool valid = false;
    };

    bool IsMacOS() noexcept;
    bool Read(uintptr_t address, void* output, size_t bytes) noexcept;
    bool ReadMethods(uintptr_t object, Methods& methods) noexcept;
    Sample Capture(uintptr_t swapchain, uintptr_t queue) noexcept;
    Layout Calibrate(const Sample& first, const Sample& second, const Methods& methods) noexcept;
    uintptr_t Resolve(uintptr_t swapchain, const Layout& layout) noexcept;
}
