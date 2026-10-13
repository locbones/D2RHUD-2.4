#include "WineQueue.h"
#include <cstring>
#include <limits>

namespace WineQueue {
    bool IsMacOS() noexcept {
        using HostVersion = void (__cdecl*)(const char**, const char**);
        const auto module = GetModuleHandleW(L"ntdll.dll");
        if (!module || !GetProcAddress(module, "wine_get_version")) return false;
        const auto host = reinterpret_cast<HostVersion>(GetProcAddress(module, "wine_get_host_version"));
        const char* system = nullptr;
        const char* release = nullptr;
        if (host) host(&system, &release);
        return system && std::strcmp(system, "Darwin") == 0;
    }

    bool Read(uintptr_t address, void* output, size_t bytes) noexcept {
        if (!address || !output || !bytes || address > UINTPTR_MAX - bytes) return false;
        const DWORD savedError = GetLastError();
        SIZE_T read = 0;
        // Wine may report MEM_FREE for readable macOS allocations. Do not gate
        // this read on VirtualQuery's Windows memory map.
        const bool result = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
            output, bytes, &read) && read == bytes;
        SetLastError(savedError);
        return result;
    }

    bool ReadMethods(uintptr_t object, Methods& methods) noexcept {
        uintptr_t table = 0;
        return Read(object, &table, sizeof(table)) && Read(table, methods.data(), sizeof(methods));
    }

    Sample Capture(uintptr_t swapchain, uintptr_t queue) noexcept {
        Sample result;
        result.queue = queue;
        for (size_t i = 0; i < ScanSlots; ++i) {
            if (swapchain > UINTPTR_MAX - i * sizeof(uintptr_t)) break;
            result.readable[i] = Read(swapchain + i * sizeof(uintptr_t), &result.slots[i], sizeof(uintptr_t));
        }
        return result;
    }

    static bool Adjustment(uintptr_t typed, uintptr_t raw, intptr_t& delta) noexcept {
        if (!typed || !raw) return false;
        const auto distance = typed >= raw ? typed - raw : raw - typed;
        if (distance > MaxAdjustment || distance % sizeof(uintptr_t)) return false;
        delta = typed >= raw ? static_cast<intptr_t>(distance) : -static_cast<intptr_t>(distance);
        return true;
    }

    Layout Calibrate(const Sample& first, const Sample& second, const Methods& methods) noexcept {
        Layout result;
        if (!first.queue || !second.queue || first.queue == second.queue) return result;
        for (const auto method : methods) if (!method) return result;
        size_t candidates = 0;
        for (size_t i = 1; i < ScanSlots; ++i) {
            intptr_t a = 0, b = 0;
            if (!first.readable[i] || !second.readable[i]
                || !Adjustment(first.queue, first.slots[i], a)
                || !Adjustment(second.queue, second.slots[i], b) || a != b) continue;
            ++candidates;
            result.offset = i * sizeof(uintptr_t);
            result.adjustment = a;
        }
        result.valid = candidates == 1;
        result.methods = methods;
        return result;
    }

    uintptr_t Resolve(uintptr_t swapchain, const Layout& layout) noexcept {
        if (!layout.valid || layout.offset == 0 || layout.offset >= ScanSlots * sizeof(uintptr_t)
            || layout.offset % sizeof(uintptr_t) || !swapchain
            || swapchain > UINTPTR_MAX - layout.offset
            || layout.adjustment < -static_cast<intptr_t>(MaxAdjustment)
            || layout.adjustment > static_cast<intptr_t>(MaxAdjustment)) return 0;
        uintptr_t raw = 0;
        if (!Read(swapchain + layout.offset, &raw, sizeof(raw)) || !raw) return 0;
        const auto distance = static_cast<uintptr_t>(layout.adjustment < 0 ? -layout.adjustment : layout.adjustment);
        if ((layout.adjustment < 0 && raw < distance)
            || (layout.adjustment >= 0 && raw > UINTPTR_MAX - distance)) return 0;
        const auto queue = layout.adjustment < 0 ? raw - distance : raw + distance;
        Methods actual{};
        // All 19 queue entries must match before the caller may invoke GetDesc
        // or retain a reference. No QueryInterface/refcount probes of guesses.
        return ReadMethods(queue, actual) && actual == layout.methods ? queue : 0;
    }
}
