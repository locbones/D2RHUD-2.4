#pragma once
#include "Inspect.h"
namespace Inspect::Native {
std::optional<uint32_t> LocalId();
bool InTown();
bool CanInspect(uint32_t type, uint32_t id);
bool CursorEligible(D2UnitStrc* unit);
struct Eye {
    uint32_t type, id;
    float x, y;
};
std::vector<Eye> Eyes(float width, float height);
void Send(const Data::CommandPacket& command);
} // namespace Inspect::Native
