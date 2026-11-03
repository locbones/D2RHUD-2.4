#pragma once
#include "Spectate.h"
#include <d3d12.h>
namespace Spectate::Video {
inline constexpr unsigned TextureSlots = 3;
bool NeedsRender();
// Called on the render thread, before the HUD is painted. Capture therefore
// includes the game's own camera/UI, never the desktop or another HUD video.
void Render(ID3D12Device*, ID3D12CommandQueue*, ID3D12Resource*, D3D12_CPU_DESCRIPTOR_HANDLE,
            D3D12_GPU_DESCRIPTOR_HANDLE, unsigned descriptorStride);
void Presented(ID3D12CommandQueue*);
} // namespace Spectate::Video
