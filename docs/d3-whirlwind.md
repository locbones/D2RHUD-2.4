# D3 style Whirlwind

Enable **D3 Style Whirlwind** in **D2RHUD Options**. The setting defaults to off
and is saved as `D3StyleWhirlwind` in the mod's HUD configuration.

Assign Whirlwind to a mouse button, hold that button, and move the cursor to
steer. Keep the cursor at your feet to spin in place. Click a nearby ground
item with the other mouse button to collect it into inventory while spinning.
For the usual right-button Whirlwind assignment, item pickup is a left click.

**Disable Right-Click Item Pickup** in D2RHUD Options is a separate saved input
preference (default off, JSON key `DisableRightClickItemPickup`). It works with
D3 Style Whirlwind either enabled or disabled. When selected, right-down and
held-right ground-item interactions stop before native walking/pickup is queued;
left-click pickup, inventory right-click actions and automatic gold collection
retain their ordinary handling. The Whirlwind click queue obeys this preference
as well, including right-clicks queued just before changing it.
Normal pickup range, collision, ownership, stacking and available inventory
space still apply. The game's **Auto Gold Pickup** setting continues collecting
eligible nearby gold while spinning, using the native selection, delay and gold
capacity checks. Other items still require a click.

The initial cast uses the game's normal validation and cost. Continued spinning
pays one native Whirlwind cast cost every 25 simulation frames (one second).
Native hit timing, weapon handling and damage remain in the original skill
routine. Cursor changes are sent immediately from the native client update;
the 40-ms heartbeat interval applies only when the cursor has not changed.
Steering discards the previous route's cached waypoint before computing the
new direction, so the active spin does not deliberately finish its old route
first. Movement still updates at the game's native 25-Hz simulation rate.
Repeated input for the held Whirlwind button keeps the current spin active
instead of predicting another cast and ending its animation. Release the
assigned button, change the assigned skill, lose focus, open an interactive
HUD/game panel, or run out of resources to end the channel. A blocked path ends
the spin through native cleanup. Disabling the option ends an owned channel on
the next simulation update and restores ordinary Whirlwind behavior.

This implementation supports the character played by the local game host,
including offline games. Joining a remote host retains ordinary Whirlwind.
Skills supplied by item charges also retain their original behavior. Custom
skills using the audited Whirlwind start/do callbacks can use the channel.
No character save format or saved skill data is changed.

Ordinary Assassin Whirlwind also includes the colleague's animation restart
fix while **D3 Style Whirlwind is off**. If the local server is still actively
Whirlwinding but the Assassin's prepared client sequence would be rejected by
state 54, the existing client start hook temporarily masks only that state bit
during its single native call and restores it immediately. Native path failures
remain failures. The fix excludes other classes/skills, remote-host sessions,
inactive server spins and the enabled D3-style channel. It requires no extra
setting, skill-table edit or character save change.

Loop-ready HD and legacy Whirlwind audio overrides and a repeated listening
preview are in [`examples/whirlwind-audio`](../examples/whirlwind-audio/README.md).
They smooth the recording's restart; installing them leaves sound playback
start/stop and looping configuration unchanged.

## Implementation and evidence

The audited executable is D2R `3.1.1.0 (91923)`, SHA-256
`2c2ddb60a40ede3aaff51afb64e932621a03e32487687b6c64d88205fa652360`.
Addresses below are relative to its module base. Function windows are verified
before installation; another build requires a new native audit.
The same windows are verified for the Downloads executable, SHA-256
`e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8`.

- Server Whirlwind start/do: `3741d0` / `374ca0` (table slots 38 / 76).
- Client Whirlwind start/do: `17bf80` / `17c310` (slots 31 / 45).
- Server/client movement: `2c5e30` / `a4f40`. Scoped hooks update the current
  native path or hold position. They leave the skill's attack frame and last
  target fields unchanged. Retargeting moves the endpoint's collision
  reservation and rolls back a failed path calculation.
  The type-7 builder at `22cec0` / `22bb80` can prepend a saved waypoint
  from the old route (`path+2b4`, points at `+2b8`, count at `+380`) when
  `path+381` is valid. Movement writes that cache at `215d92..215dd7`.
  Retargeting invalidates only byte `+381` before computing; native `1f7c30`
  otherwise clears it after the builder has already consumed it. Failed
  retargets restore the complete prior cache along with the path snapshot.
- Native mouse-to-world conversion: `6e5e0`, using the same screen coordinates
  as the normal mouse dispatcher, including native HD and classic handling.
  Native panel state at `1d53fb8` prevents world input through menus; HUD,
  automap, item-label and help-button display remain allowed. Native panel 20
  is `MiniMenuPanel`, the bottom HUD button strip, and its ordinary visibility
  must not block channel activation or steering. The renderer still blocks
  input captured by an actual HUD menu. WndProc publishes
  clicked item IDs atomically so even a click shorter than a simulation tick
  can be consumed without replacing the native movement action.
- Client action dispatcher: `a6120`. The first control message is queued after
  the ordinary cast packet, since this function predicts the skill before
  queuing its packet. Once the channel owns the held button, repeat actions for
  that button are consumed before native prediction can finish the active
  skill. Other skills, the other button and forced attacks retain their native
  action path. Ground-item actions are intercepted before their normal
  animation/path changes when this feature owns the channel.
- Server packet dispatcher: `2c1ce0`. Whirlwind sets state 54, which otherwise
  prevents pickup packets from reaching their handler. The scoped bypass calls
  the original `0x16` pickup handler at `2aed60`; it retains the full native
  transaction and inventory checks in `2d59f0`. The current build's pickup
  packet is 17 bytes, not the layout in older Diablo II sources.
- Native auto-gold update: `1023c0`; gold-only selection: `1020b0`.
  The update retains the game's Auto Gold setting, current/max gold, collection
  delays and occupied-cursor checks. It sets its selected gold target at
  `1dfe2b0` only around the interaction call (`102704..10272f`). A scoped hook
  at the interaction entry `d3e30` routes action 7 for that target through the
  existing direct pickup request when our local channel is held. It preserves
  movement/animation and the authoritative server inventory/gold transaction.
  Native `b2f40` owns the manager, whose target is at object offset `bf0`.
  These function entries are signature-checked. No extra scanner or pickup
  timer is added, and other interactions retain their original handling.
- Native cost/use helpers: `21df90` / `294e70`. Existing HUD mana hooks remain
  in that call path. Blood Mana cannot kill the character through the extra
  channel charge. Charged item skills are excluded.

Client and server both require an actual native Whirlwind cast before channel
control begins. Control messages bind to the current game, player, skill and
connected client, use monotonically ordered sequences, and expire after
400 ms without a heartbeat. Release latches the current cast closed. Gameplay
memory changes occur on native simulation callbacks; the renderer only
publishes HUD input-capture state. The option adds no notes below the Options
checkboxes.

## Verification

From an x64 Visual Studio developer prompt:

```bat
tests\run-d3-whirlwind.cmd <output-directory>
python tests\generate-d3-whirlwind-native.py --check-compatible <D2R.exe>
tests\run-d3-whirlwind.cmd <output-directory> <D2R.exe>
```

The fixtures check malformed/replayed control messages, authentication,
timeouts, release, stationary spinning, bounded steering, reservation rollback,
unchanged damage timers, mana/Blood Mana, exact pickup packets, and native
inventory-handler delegation. They also check immediate cursor publication,
rapid direction reversals before the old endpoint, and repeated held-button
actions without restarting the cast or its damage/mana schedule. A route-cache
fixture also exercises a cached old first waypoint with a new final goal,
including cache restoration after failed client and server retargets. A guard-page
fixture checks that local session
discovery reads only the actual native client prefix.
Activation tests use the panel state captured from the running game, including
the visible bottom HUD strip, and verify that inventory, pause, and Options
input capture still stop the channel. An all-zero panel fixture would miss
the activation failure caused by treating `MiniMenuPanel` as a modal menu.

The optional executable argument also tests the real MinHook installation and
uninstallation of all nine hooks in a private mapped copy of the executable,
alongside an existing mana detour. It verifies restoration of the original code
and rejection of a changed compatibility signature. The install fixture does
not execute game code. The separate auto-gold fixture executes the actual native
update in a private mapped image, with configuration, lookup, allocation and
packet I/O mocked. It checks enabled/disabled Auto Gold, gold capacity, delays,
occupied cursor and normal delegation when D3 Whirlwind is disabled. Core tests
also verify both mouse assignments, unchanged path/hit state, gold-only routing,
exact packets and range rejection. No running game is modified by these tests.

Static disassembly and mocked native fixtures establish these code paths; they
do not establish live game feel. In-game acceptance still needs a character
with Whirlwind: turn in several directions while holding, stop at the cursor,
release, pick up nearby items and gold, try a full inventory and a blocked item,
hit a wall, exhaust mana, alt-tab, and disable the option mid-spin. Repeat with
the option off to compare ordinary Whirlwind. No game was launched by the
implementation tests.

### Colleague Assassin restart integration

Source: `D2RHUD-gold-tz-whirlwind-source.zip`, SHA-256
`6995acfd86b045aa3bd6651224479fc98b090da3c6a0a0638b9bfa6a2bf95ef3`.
Its `src/whirlwind_restart.h` is included unchanged as
`plugin/D3WhirlwindRestart.h` (SHA-256
`d5c6cdf3a11d61e3204c16632d84808ef5925381383343d5b5c6d57fa48f004f`).
`ClientStartHook` calls `WhirlwindRestart::Invoke` with its existing original
trampoline, module base and current option value. The archive's binary-specific
adapters, trace ring, compiled DLLs and reconstruction scripts are not build
inputs. The repository already supplies the auto-gold and Terror Zone rotation
behavior retained by that archive.

The helper verifies PE identity and the unchanged native state-54 guard before
masking the local Assassin's bit. It requires client sequence mode 18, skill 151,
inactive client skill flags and the matching authoritative server player with an
active Whirlwind. It restores the bit only if the same stat-list pointer still
belongs to the unit, preserving unrelated changes during the call.

The optional executable test runs this integrated hook against the real native
start guard and success/failure branches in a private mapped image, with only
path/resource providers stubbed. It verifies exactly one native call, successful
restart, blocked-path failure, state restoration, unrelated-bit preservation,
stat-list replacement and 17 exclusion cases. Existing channel, pickup, gold and
hook installation regressions run alongside it. Live ordinary Assassin animation
still needs an in-game check with D3 Style Whirlwind disabled.
