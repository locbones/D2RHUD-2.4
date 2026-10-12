# CrossOver and D3DMetal compatibility

D3DMetal can expose a command-queue implementation in host memory that Windows
Detours cannot patch. On Wine/macOS, D2RHUD calibrates the wrapped queue pointer
using two temporary swapchains with distinct queues. The reader requires a
unique layout, validates it against both known queues, and checks all queue
vtable entries before using the live queue. It does not call COM methods on
scan candidates or assume a fixed private swapchain offset.

This follows the wrapped-pointer idea discussed in
[REFramework PR #1847](https://github.com/praydog/REFramework/pull/1847), using
independent, read-only calibration. Native Windows retains the
`ExecuteCommandLists` hook path. Rendering starts only after the required
hooks and plugin state are ready, and the original `Present` runs outside the
HUD's locks.

Item-filter initialization, Lua output, Grail initialization and shared-stash
logic retain the upstream implementation. There is no item-filter compatibility
switch or alternative installation path.

## Deployment

D2RLAN can overwrite custom HUD builds. Disable its update checks, turn off
HUD Debug, and replace both `D2RHUD_RELEASE.dll` and `D2RHUD.dll` in its launcher
directory while the game is closed. Remove the old generated `d2rhudb.dll`
before restarting so the loader does not retain an older copy by timestamp.

See [the standalone tests](../tests/README.md) for queue and hook-chain
checks. They do not replace in-game testing.
