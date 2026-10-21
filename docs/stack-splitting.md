# Right-click stack splitting

**Quantity Right-Clicks** in **D2RHUD Options** enables this helper. It defaults
to on, applies immediately, and is saved as `QuantityRightClicks` in the mod's
HUD config. Disabling it restores ordinary right-click behavior for stacks and
closes any active chooser, keeping an already completed one-item split.

The indented **Minimalist Quantity Popup** option selects the previous compact
layout without item images. It defaults to off, applies immediately, and is
saved as `MinimalistQuantityPopup` in the same HUD config.

Right-click a stack with quantity greater than one and **Transmogrify disabled**
to separate one item into a free storage slot and open a small gothic quantity
panel immediately above the clicked item. It uses the game's modal frame,
gold-entry field, arrow and confirmation artwork, plus the mod's close button
and gothic font. The default view shows matching item icons on both sides:
**Keep** on the left, **New stack** on the right, with an arrow pointing toward
the new stack. The left quantity updates as you edit the new quantity beneath
the right icon. At 1080p the visual view measures approximately 307 by 213 pixels;
minimalist mode measures approximately 281 by 114 pixels. Both keep the previous
30% text/control enlargement, with a number field sized for three or four digits.
The panel scales with display height and stays on screen.
It has no dim layer or automatic keyboard focus. Use the up/down arrows or click
the number to type, then **Split** or Enter.
For example, an 88-item stack first becomes 87 + 1; applying 20 makes it 68 + 20.
**x**, Escape, an outside click, or the one-minute timeout leaves the initial split.
Outside clicks dismiss the panel and continue to the game.

The destination is a free slot in the original storage page, falling back to
the player's inventory. Personal inventory, cube and personal stash pages are
supported. A full destination leaves the original stack unchanged and displays
an error. Existing stacks are not merged and items are not dropped on the ground.
Transmog-enabled items keep their conversion behavior. Quantity-one items keep
their ordinary behavior. Items containing nested items cannot be split.
Town Portal and Identify tomes (`tbk` and `ibk`) keep their ordinary right-click
use and are excluded from splitting, regardless of their scroll quantity.

This uses native stackable/quantity-bearing item definitions, including custom
mod items. Native saved-item serialization copies quality and saved modifiers
into a new unit with its own GUID and saved fingerprint. The original unit remains in its slot.
Both quantities are republished to the client. No TXT/JSON edits, companion
files, new recipes or changes to the character save format are needed.

Like the existing right-click conversion, this version supports offline games
and the local TCP/IP host character. Joining clients and online sessions do not
use this helper. Only mouse input over the panel and keyboard input while editing
its number are captured. Final application revalidates the game, both item IDs/seeds, ownership,
inventory state and quantities; changed stacks keep the initial split.

Install the new `d2rhud.dll` with D2R closed and fully relaunch. Keep the launcher's
**Disable update checks** setting enabled for local HUD builds. No launcher or
game executable edits are involved. The feature report includes a stack-splitting
status line, and eligible item tooltips show the right-click action.

## Verification

`tests\run-legacy-transmog.cmd <output-directory> "<supported D2R.exe>"`
includes `stack-split-test.inl`. It covers the 88 -> 87+1 -> 68+20 transaction,
cancel, no-space rollback, inventory/cube/stash, creation failures, stale requests,
ownership/quantity checks, transmog priority, duplicate packets and window-thread
submission. A private mapped executable exercises the native save wrapper/bit
writer, saved-header parser, allocation/load wrapper and mode setter. The item
body serializer/loader and allocator are boundaries in that fixture; existing
native inventory and notification fixtures also run. No running save is modified
by these tests.

`tests\run-stack-split-ui.cmd <output-directory> <sprite-fixture-directory> [font.otf]`
verifies the panel at 720p, 1080p and 4K, screen-edge positioning, no modal/popup
stack or full-screen dim geometry, no automatic focus, and typing/submitting
two- and four-digit quantities. Fixtures are the six sprite files named in
`StackSplitSkin.cpp`, extracted from the user's installation. Native sprite
decoding checks include the separator pixels between button frames and truncated
data rejection. A seventh sprite, `goldbar.sprite`, exercises both item previews.
Both layouts check Enter and Split-button submission, and minimalist mode verifies
that no item artwork is requested. Safe path tests cover custom and ordinary
HD item mappings and reject traversal/absolute paths. Rendering uses ImGui image/input primitives with the game's
artwork, rather than native game widgets. Frame artwork and `hd/items/items.json`
load once from the active mod's loose files or installed CASC data. Item sprites
load on demand on a worker using the copied full item code, including custom
four-character mappings; the render thread never reads a live item pointer or
opens CASC files. Missing artwork retains labeled quantities and the same split
behavior. A dedicated reusable GPU slot avoids filling the buff icon cache as
different items are split. No additional asset distribution is needed.

Live acceptance still needs a full D2R restart: split an ordinary and custom
stack, choose 20 from 88, test full storage, and save/re-enter to verify both
quantities and modifiers. Check that a transmog-enabled stack still converts.
