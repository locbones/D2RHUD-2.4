# Native marketplace chat bridge — D2R 2.4

The installed executable SHA-256 is
`e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8`.
This evidence comes from an ordinary offline PE/Capstone audit. REA/Ghidra
analysis timed out; it did not provide decompiler evidence for these claims.
`tests/marketplace-chat-native-test.py` reproduces the checks without attaching
to or writing the running game.

- RVA `0x11f590` receives the native chat text in RDX. Both direct callers
  (`0x74ea5`, `0x1c385e`) pass four registers and ignore the return value.
  The function checks native slash strings including `/fps` and `/players`
  before constructing the chat packet and calling `0x10ebf0` at `0x1223d9`.
  The first packet-only implementation did not receive `/market` during the
  user's live test. Private commands must be consumed at this earlier entry.
- RVA `0x86860` was observed as a general UI-close routine. The first early
  submit build used it and the user's test crashed, without a fresh local dump.
  The current bridge does not call it. Dismissal is deferred until the submit
  stack has returned, using the existing synthetic Escape helper and only when
  the native UI_CHATBOX flag (ID 5) is still open.
- A later checkpoint stopped at the legacy raw-global `PrintGameMessage`
  adapter. The current bridge calls `GetChatManager` (`0x56e440`) first, as the
  vanilla submit dispatcher does at `0x1211b1`, `0x12157a`, `0x12193d`, and other
  display paths. This getter initializes the native ring buffers before returning
  the global manager address. A global address alone does not initialize them.
  The bridge passes a borrowed string, the stable global Game message type and
  four fully zeroed 16-byte optional values to `ChatManager_PushChatEntry`.
- Native presentation is invoked after releasing the mailbox mutex. The
  regression test re-enters both private command handling and Tick during display;
  it also verifies that the delivery cursor advances only after display returns.
  Calling native presentation while holding this mutex could deadlock on callbacks.
- RVA `0x10dce0` remains the common client packet queue. Its interception is
  a secondary private-command guard. All other packets retain their trampoline.
- Native received text uses the already established `GetChatManager` and
  `ChatManager_PushChatEntry` ABI and Game message type in this repository.
  No guessed native Trade-channel registry or overlay text renderer is added.

The submit and queue prefixes are checked before attaching hooks.
Unavailable hooks never prevent launch or inventory operations. The mailbox
bridge has no HTTP client, inventory pointers, background threads or shutdown
work. Only the companion communicates with the service. Private command text
is consumed before the native slash dispatcher and network channels.

## Live hang capture and presentation thread

On 2026-11-01 the user reproduced the failure with the Windows debugger attached.
No exception was raised. A local thread snapshot found the render thread blocked
in `RtlAcquireSRWLockShared`, reached through native font/layout and
`ChatManager_PushChatEntry` (`D2R+0x56edf5`). Its caller was the marketplace sink
inside `Bridge::Tick`, `D2RHUD::OnDraw`, and `D3D12::DrawPresent`. This is a native
resource-lock deadlock, separate from the mailbox lock. The game process remained
alive and unresponsive.

Presentation now runs from a window-owned WM_TIMER callback created with
`SetTimer(hwnd,...)`, following the repository's existing startup-command
window-event dispatch. Present only schedules the timer. The callback initializes
the native chat manager, emits messages and records acknowledgements after it
returns. This avoids requesting native UI/font layout while Present holds its
resource lock. Native chat does not render from the graphics callback.
