# Native linked-item display, D2R 2.4

Static PE observations from the installed D2R.exe, SHA-256
`e4e13570e60c0a65cb0469d6659be8a6008f9433ac50db1c4f94501d7d9016f8`.
Addresses are RVAs; decoded with pefile/Capstone. These are not runtime
observations. REA objdump is unavailable on Windows; bounded raw-x64 Ghidra
analysis is unsupported by the Windows provider.

- TextBoxWithLinksWidget vtable 0x15b1d60, size 0x750. Draw 0x5b0f30,
  OnMessage 0x5b0fb0, constructor 0x5b0da0. The native chat layouts use
  text/style for the embedded TextBox at +0x538.
- Link vector +0x520/+0x528/+0x530 is pointer/count/capacity. One link record
  is 0x440 bytes: start/character count at +0/+4, URL string +8, serialized
  item bytes[1024] +0x30, byte length +0x430, isItem +0x438.
- Destructor helper 0x5b1f00 clears each isItem byte, frees URL only when
  its capacity is nonnegative, and frees the vector only when its capacity
  is nonnegative. Dedicated marketplace vectors/empty URLs use the external
  ownership high bit and stable process-lifetime storage.
- Embedded caption string +0x5c0 is updated through native string assignment
  0x622d0(pointer,text,length), preserving the game's allocation ownership.
- Link-bound builder 0x5b1940 populates rectangle vector +0x738/+0x740.
  Rectangles are x/y/width/height, verified in hit test 0x5b1870.
- Link click 0x5b10c0(widget,packedPoint) is also called by OnMessage. It
  copies the selected record, decodes raw item bytes, reconstructs a UI preview
  unit and socket children, invokes native item text builder 0x1bb940 and opens
  DismissibleTooltipPanel. It then removes/destroys the temporary preview units.
  No chat submission or save transfer is used by the marketplace bridge.
- Exact item bytes come from the existing audited save parser/native serializer,
  including children. Oversized records are rejected rather than truncated.
- Native routine fingerprints guard click, string assignment, bound building and
  destruction layout; class/vtable checks reject other controls. Icon actions
  run on the native bank message thread, never the model/render thread.

Isolated tests verify record offsets, exact bytes, UTF-8 character length, limits,
external ownership, caption deduplication and rejection of foreign vectors.
Fixture tests do not establish native rendering or tooltip behavior in-game.

## Ordinary label cleanup

A shutdown audit found the old ordinary-label bridge overwrote the native
TextBox string pointer at +0x88 with a pointer into a HUD static buffer, while
leaving native capacity/ownership intact. This both lost the previous allocation
and allowed the game's destructor to free memory owned by the HUD.

Verified on the same executable: TextBoxWidget constructor 0x57a960 installs
vtable 0x15acb98 and initializes pointer +0x88, byte length +0x90, capacity
+0x98 and inline storage +0xa0. Destructor 0x57acf0 tests the capacity sign
at 0x57acfa; for nonnegative capacity it frees the pointer at +0x88 via the
game allocator. Draw is 0x57b700. The fix uses native string assignment
0x622d0 and compares existing text to avoid an assignment every paint.
It guards the TextBox class, assignment tail and destructor ownership branch.
InputTextBox and linked-item records retain their separate existing bindings.

The native fixture now models inline and heap string ownership, checks long and
short replacements, unchanged paints, native byte length and native destruction.
The previous raw-pointer implementation fails that ownership contract.

This establishes a cleanup defect, not a measured in-game Alt+F4 fix. An
independent source and isolated DLL timing audit ruled out the marketplace
std::async futures as a shutdown wait in the tested /MD and /MT DLL configurations.
The old local crash log records a graphics teardown crash from an earlier build;
it does not attribute the current shutdown delay to a particular call.
