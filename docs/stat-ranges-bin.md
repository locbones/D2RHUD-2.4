# Stat ranges with compiled tables

Stat-range tooltips accept the game's D2R 2.4 TXT or BIN tables. The selected
mod's BIN overrides source TXT from a lower-priority data layer. Within a layer,
TXT remains preferred. The numeric BIN path requires neither TXT files, `-txt`,
nor a populated native name linker.

The reader decodes these compiled tables:

- ItemStatCost, Properties and the suffix/prefix/automagic tables.
- QualityItems, CharStats, Weapons, Armor, Misc and ItemTypes.
- Runes, Gems, UniqueItems, SetItems and Sets.
- SkillDesc and Skills.

BIN property/stat/type/item references and saved modifier ranges are read
from their serialized numeric IDs. Item names retained by the native format
are read directly. Localization references use the native 16-bit string IDs
and the installed localization JSONs, preserving all translated tooltip text.

Compiled table sizes and row counts are validated before decoding. Bad or
unsupported tables produce a stat-range loading error instead of guessed data.
Source `*ID` columns are comments: compiled IDs/ordinals are authoritative.
Native unique/set BIN row ID fields are initialized at runtime and are zero
on disk, so these tables use their compiled row ordinals.

For a mixed TXT/BIN installation, symbolic property/stat/set/description names
that the compiler removes can be supplied by the shared, checksum-bound table
metadata generated for the mod. Pure BIN installations use numeric references
and do not need these source names for ranges. Numeric compiled values remain
authoritative even if a source comment or named parameter differs from the BIN.

From an x64 Visual Studio developer prompt, run:

```bat
tests\run-stat-ranges-bin.cmd <output-directory> "<RMD-MP global\excel directory>"
```

The focused fixture test reads the current mod's 18 table pairs and localization
JSONs. It exercises a pure BIN installation with no source names, each table
individually replaced by BIN in an otherwise TXT setup, alternating mixed
formats, every table being truncated, and localized unique/gem/poison ranges.
The existing `run-stat-ranges.cmd` suite covers the tooltip/layout/input paths.
