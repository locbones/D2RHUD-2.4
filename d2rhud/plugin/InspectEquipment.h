#pragma once
#include "../d2/D2Ptrs.h"
namespace Inspect::Equipment {
template <class Reader> bool Capable(const D2UnitStrc& unit, Reader&& read) {
    if (unit.dwUnitType == 0) return true; // Players always support equipment.
    if (unit.dwUnitType != 1 || !unit.pMonsterData) return false;
    D2MonStatsTxt* record = nullptr;
    uint32_t flags = 0;
    // Use this unit's loaded MonStats record. Inventory existence alone does
    // not establish equipment capability; an enabled but empty unit is valid.
    return read(&unit.pMonsterData->pMonstatsTxt, record) && record &&
           read(&record->dwMonStatsFlags, flags) && (flags & MONSTATSFLAG_INVENTORY) != 0;
}
} // namespace Inspect::Equipment
