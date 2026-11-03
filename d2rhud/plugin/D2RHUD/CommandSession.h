#pragma once
#include <cstdint>
#include "../../SessionEpoch.h"

namespace HudCommands {
// A client may be in a game without owning its server token (TCP/IP joiner).
// Use a generation for queue ownership, including observed exits/reentries that
// reuse the same native token. The command module serializes access.
using SessionTracker=HudSession::Tracker;
}
