# Shutdown regression investigation

The user observed several seconds of delay after Alt+F4, only after using
the stash or marketplace. The investigation found two cleanup defects.

## Observed graphics teardown crash

The existing local crash report was updated during the investigation at
2026-10-31T09:31:57 UTC. It reports an access violation in
D3D12Core.dll+0x53b3e, with a return address in the installed HUD at
RVA0x3eaa56. Static decoding of that installed DLL confirms the caller at
0x3eaa40 reads a namespace-global pointer, tests it and invokes vtable+0x10
(COM Release) at0x3eaa53. CRT cleanup and DLL detach frames follow it in
the recorded stack. This is a shutdown exception, not a captured wait stack.
The crash handler flushes a report and writes a minidump, which can add
visible exit time. Its exact contribution to the user's delay was not measured.

D3D12Hook.cpp previously declared namespace-static CComPtr objects and
containers containing them. The DllMain process-termination guard skips
explicit unhooking, but CRT static destructors still release those objects
afterward, under loader-lock shutdown with other threads already terminated.
All retained GPU owners now live in a single process-lifetime GraphicsResources
state. Local temporary COM pointers retain RAII. ResetState still releases
all retained resources on resize and explicit unload; normal process exit
leaves the one state allocated for the OS to reclaim.

The isolated process-lifetime regression uses ATL CComPtr<IUnknown> probes
in direct fields, arrays, frame vectors, maps and aggregate texture records.
It tests no late Release for retained state, normal Release for explicit
reset, and an ordinary-static positive control that delays on Release.
It does not execute the real graphics driver or prove actual game exit timing.

## Native marketplace labels

The ordinary-label binder replaced a game-owned string pointer with a HUD
static-buffer pointer without changing native ownership. The native TextBox
destructor can therefore free HUD-owned memory, and the old allocation leaks.
MarketplaceNativeText.inl now uses the game's native string assignment routine,
retains native allocation ownership and skips unchanged captions. The native
fixture covers inline/heap ownership, replacements, byte length and destruction.
Exact native layout observations are in native-item-links-evidence.md.

## Background jobs ruled out in isolated DLL tests

Marketplace futures live in a heap EditorState and are not destroyed at exit.
The installed MSVC task scheduler does not block DLL shutdown on pending tasks.
An isolated DLL with a five-second async job exited in20ms (/MD) and25ms
(/MT); an /MT EXE positive control took5016ms. No background-loader rewrite
was made. Source and harness observations are retained under the ignored
build-marketplace/shutdown-audit directory.

The final game's Alt+F4 behavior still needs confirmation with the installed
fix; neither isolated fixture claims to have launched or closed the game.

## Startup correction

The first graphics-lifetime build introduced a launch failure. A new report at
2026-10-31T09:42:35 UTC points to ResetState, installed-HUD RVA0x45058.
Its array cleanup read from image RVA0x50d5e0 and treated that value as a COM
object. The matching linker map identifies that address as a resource reference,
not an element of the intended texture array. The previous launchable DLL was
restored before further work, and the failed DLL, PDB, map and report preserved.

A reduced clean compiler fixture reproduces incorrect namespace-array aliases
without incremental linking, so stale linker state alone is not an established
cause. The fix removes every namespace resource-reference alias. Graphics() now
lazily obtains the owner and every use accesses its fields directly. The corrected
release is fully linked, and inspection confirms ResetState accesses fields
through that getter; the obsolete array/reference symbols are absent.

The process-lifetime test now exercises reset of a completely empty startup
state before population, native ATL output-pointer assignment, independent array
elements and repeated reset/refill. A headless production-reset graphics smoke
test is also used when available; these tests do not launch the game.
