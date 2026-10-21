# Automatic stack socketing

Dropping a stack of socket fillers onto a socketed item now inserts **one**
filler and returns the remaining quantity to a free inventory cell. For example,
88 runes become one socketed rune and an inventory stack of 87. Stackable gems,
runes, jewels and custom socket fillers follow the native socket-filler check.
The original filler GUID stays in the socket; the remainder receives its own
GUID and saved fingerprint, with quality and saved modifiers copied through
the native saved-item codec.

The feature is automatic and independent of **Quantity Right-Clicks**. It runs
on the authoritative offline/TCP-IP server, including for joining players on
that host. It does not operate on Battle.net servers. Existing single items,
ordinary stack merging, item conversions and other item-on-item operations
retain their normal behavior.

The helper verifies the owner, cursor, recipient and actual item position, then
uses the native socket eligibility classifier. It reserves the remainder in
inventory before passing quantity one through the original socket command.
Native socket effects, runeword handling and socket notifications continue
through the game's original code. There is no quantity chooser or new recipe.

If inventory is full or the saved-item copy fails, the socket attempt is
refused and the original stack stays on the cursor. If the native socket
command refuses after splitting, the helper removes the remainder before
restoring the original quantity. If another inventory change prevents that
rollback, both legitimate split stacks are retained; it never restores the
full quantity alongside an attached remainder. Cursor quantities use the
native variable-width unit-stat packet, without moving the item into an
inventory slot or truncating quantities to a byte.

Replace `d2rhud.dll` with D2R closed and restart. No TXT/JSON, launcher, game
executable or save-format changes are needed. The feature report includes
**Automatic stack socketing** and reports incompatible native signatures.

`tests/run-legacy-transmog.cmd <output-directory> "<supported D2R.exe>"`
includes stack-socketing cases for quantities 2/88/9999/100000, independent
right-click settings, full inventory, copy failures, native refusal, rollback
failure, ownership/position/replay checks and remote TCP/IP players. A private
mapped executable executes the real socket packet entry, eligibility check,
socket parser/insertion/mode routines, saved-item wrappers and variable-width
quantity packet writer. Inventory grid application, item property attachment,
item-body codec, allocator and packet transport are controlled boundaries.
These checks do not replace an in-game acceptance test.
