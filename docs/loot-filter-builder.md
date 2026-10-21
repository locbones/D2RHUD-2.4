# Building a loot filter in the HUD

Open the loot filter editor and select **Rules**. Choose **Add rule**, give it a
name, then use **Add a condition** and **Add an action** to build it. **Save and
apply** saves the filter and reloads it. The previous file is kept as a `.bak`.

The rule library stays on the left. On the right, **Conditions**, **Actions**,
**Preview** and **Rule code** each get the full workspace width and their own
scrolling area. Save controls stay above the editor. Native Diablo panel artwork,
section pictograms and separate field cards make the groups easier to recognize.
The editor uses the HUD's 18px menu font (scaled at 4K).

File import, filter creation, sounds and updates are under **Files, sounds &
updates...**. Routine file details are under **Status**; save errors open that
popover automatically. The old duplicate HUD Settings filter screen redirects to
this same full-size editor.

- Choose base items by name with the searchable **Specific items** selector.
  The lists include the current mod's custom items and four-character codes.
- Select **Add named unique / set...** to start with a specific named item. Its
  base item, quality and internal index are filled in automatically.
- Choose qualities, categories, tiers, locations, difficulties or areas. Select
  several values in a dropdown to match any of them; **Is none of** excludes them.
- Use comparisons for item levels and sockets. Item/player stat conditions have
  searchable stat names, comparison dropdowns and values, including ranges.
- Actions include hiding items, adding name/tooltip text, notifications, audio,
  label backgrounds, borders, and animated name/background effects. Text actions
  have menus for inserting colors and item details.

All conditions on a rule must match. The default location is ground items.
Rules run from top to bottom; a hide action stops evaluation. Use **Rule
actions...** for **Move up**, **Move down**, **Duplicate** and **Remove**, or use
**Rule enabled** in Conditions.

Common **Settings** use checkboxes, dropdowns and numeric controls. Speech and
sound actions also require **Enable speech and sounds**, and the existing audio
service. The builder does not install that service.

The **Preview** view updates with your draft before saving. It compares
sample items across qualities and item types, including the selected custom
base items. Cards show the original name, resulting label, and whether the item
matches, stays unchanged, or is hidden. Item-type pictograms and quality accent
bars help distinguish the samples; these symbols represent categories, rather
than the exact item inventory sprite. Colors, borders and effects use the
installed filter engine. Hover for example rolls, tooltip text and notification
details. No chat messages or sounds are sent by the preview.

Matching cards appear first. **Matches only** narrows the overview; **All rolls**
also shows the contrasting rolls normally grouped into the same item variant.
**Sample context...** opens controls for difficulty, area, player/filter level,
location, and the sample base item. These are simulated items, with example
stats; this view evaluates only the selected rule, not the full filter order or
the mod's override rules. Authored functions and unresolved custom expressions
show a preview-unavailable message and remain preserved in the draft.

Existing custom expressions are preserved. Unsupported expressions show a
**Use selections** button that replaces only that field when clicked. Other
fields, comments, translations and generated source remain intact. Advanced
users can still open **Rule code** or use the **Source** tab.

Catalog files are loaded on an asset-reader worker only when the editor needs
them. Nothing is extracted or rewritten in the mod's game-data directories.
The header artwork shares the existing stable native panel atlas; it does not
request per-item GPU textures or additional game assets while drawing the gallery.

UI verification includes all four workspace views inside a 1200x720 menu, 18px
and 22px text, a 960px-wide fallback and the 4K menu scale. The headless editor
test can optionally capture a native panel fixture with its texture IDs:
`lootfilter-editor-test.exe <capture-directory> <lootfilter.lua> [panel.rgba] [font.otf]`.
The optional fixture has uint32 width/height followed by RGBA pixels from the
game's `modal_bg.sprite`. Captured draw commands contain start/count, clip rect,
then a uint64 texture ID (1 = font atlas, 2 = optional panel).
