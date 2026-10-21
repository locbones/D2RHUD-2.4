# TXT and BIN data support

D2RHUD prefers a selected mod's data over install-level or retail files, then
prefers TXT over BIN within that same layer. A mod's BIN never loses to retail
TXT. Supported native layouts are the project's D2R 2.4 executable/table format.
Stat Ranges additionally prefers BIN within its selected layer, matching the
game's compiled data even when stale or incomplete TXT files are left behind.
Cube/quest recipe source also comes from the matching BIN companion on normal
launches; source TXT is preferred during `-txt` compilation.
Unsupported or malformed records are rejected rather than interpreted using a
different layout.

Direct BIN readers cover buff icons, stat ranges (all eighteen input tables),
grail entries, item/filter-builder catalogs, rune deposits, terror-zone base
treasure classes, Survivor skill names, and compiled Workshop class/act checks.
Existing right-click conversion, stack splitting/socketing and summon storage
already use the game's loaded native records.

## Source-only custom settings

The game does **not** compile every TXT column. Custom terror treasure-class
columns, recipe comments, `vis=`/`map=` source options and source symbol names
cannot be recovered from BIN files alone.

With the HUD loaded, a `-txt` launch (including `-direct -txt`) now automatically
exports the companions as the native startup table loader finishes. A guarded
completion callback handles short compilation runs; directory notifications
also refresh later compilation batches after their writes settle. For `-direct -txt`
without `-mod`, output is `D2R/data/D2RHUD/`; a named mod writes into its own
`data/D2RHUD/` folder. No Python installation or launcher change is required.
The worker watches the table directory and regenerates after later compilation
batches in the same process. Editing TXT alone after compilation does not bind
those uncompiled edits to the old BIN files. It performs no polling scans
while the directory is idle and does not rewrite unchanged JSON.

Writes are staged and each complete JSON file is replaced atomically. An
incomplete/inconsistent input leaves the previous companions intact until the
tables change again; temporarily locked outputs are retried automatically.
The local `%LOCALAPPDATA%/D2RHUD/table-metadata-export.log` records the last
export result and data folder. The startup feature report also records whether
automatic export was enabled. Ordinary BIN-only launches do not run the exporter.

For packaging without running the game, the manual exporter remains available:

```powershell
python tools/export-table-metadata.py "C:/path/to/Mod.mpq/data"
```

Run this after generating the matching BIN files and before removing TXT from
the release. Include the automatically or manually generated `data/D2RHUD/table-metadata.json` with the
mod. Each table entry is bound to its exact BIN length and FNV-1a checksum;
stale entries are ignored. No character save format changes are involved.

The file contains symbol arrays and the custom terror columns. It also retains
the exact original cubemain source, because quest-history identity hashes those
rows. This preserves existing journal progress when switching from TXT to BIN.
Native gameplay values for the other features are decoded from the BINs.

`transmog_table.txt` is a HUD-specific asset mapping, with no native BIN format.
The exporter also creates `data/D2RHUD/transmog_table.json` when that mapping
exists. Include it to retain the legacy transmog/extended-itemcode fallback
without shipping the TXT mapping. Ordinary JSON configuration, localization,
HD scene files, filters and art assets remain necessary.

The feature-status report includes a `TXT/BIN table metadata` line to help
compare installations. Missing terror overrides leave normal drops intact;
missing or mismatched quest/recipe metadata leaves those extensions inactive.

## Workshop compiled packages

An already compiled five-act or six-act package can load without `-txt` or
source templates. The loaded tables determine the process's act count. Changing
between five and six acts requires matching compiled data; the checkbox cannot
compile absent source files. Class identity checks also accept retained names
in the compiled class tables. Existing non-table UI/scene assets are still used.

## Verification

`table-metadata-test.cpp` exercises BIN-only symbol/source loading, exact quest
source preservation, all TXT/BIN combinations for monster and superunique drop
tables, generated native treasure-class IDs, stale/malformed metadata rejection,
and mod-versus-install layer isolation. The catalog and stat-range BIN suites
use the actual exported mod tables. Native cube/map, journal, class and Act VI
regressions cover compiled lookup fallbacks and existing save transactions.
