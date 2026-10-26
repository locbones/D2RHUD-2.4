# Tests

Run the Windows C++ fixtures from an **x64 Native Tools Command Prompt for
Visual Studio 2022**. Restore the dependencies in
[../d2rhud/vcpkg.json](../d2rhud/vcpkg.json) first. Use a temporary directory for
compiled test output; build products and caches are ignored by Git.

## Examples

Queue tests take the static dependency directory and an output directory:

```bat
tests\run.cmd <dependency-directory> "%TEMP%\d2rhud-queue-tests"
```

Native compatibility suites also accept the local game executable:

```bat
tests\run-cube-map.cmd "%TEMP%\d2rhud-map-tests" "C:\path\to\D2R.exe"
tests\run-candy-corn-save.cmd "%TEMP%\d2rhud-candy-tests" "C:\path\to\D2R.exe"
```

The Candy Corn suite optionally accepts a compiled Excel data folder and a
character-save folder as its last two arguments. It reads those saves without
writing them; conversions are tested in memory.

Loot filter tests can use this repository's engine:

```bat
tests\run-lootfilter.cmd <dependency-directory> "%TEMP%\d2rhud-filter-tests" <filter-examples-directory> lootfilter.lua
```

UI fixtures may need extracted mod artwork, fonts, or table paths. Read each
`run-*.cmd` script for its arguments and dependency directory. Some scripts take
an explicit dependency directory; others use the existing repository-local
vcpkg installation layout.

## Coverage

Suites cover configuration, UI layout/input, loot filters, item serialization,
stack splitting/socketing, saved companions, cube maps/events, quests,
Whirlwind, extended tables/classes/acts, and native compatibility guards.
Native fixtures use private executable mappings and bounded engine stubs;
they do not replace live gameplay and multiplayer checks.

`run-quest-recipe-source.cmd <output> <mod-data>` exercises the installed BIN-only
recipe annotations. Unrelated recipe edits, appended rows and stale BIN
checksums/sizes must not disable quests or change quest definition identity.
It uses the project's CascLib/zlib paths; set `CL`/`LINK` if these dependencies
are outside the restored package directory. Native cube tests check appended
ordinary recipes and actual quest gate mismatches. Journal tests cover progress
reload, rollback, duplicate rewards and missing-source diagnostics.

Act VI startup tests call the production `Install` and `Configure` entry points
with real files, including `"enabled": True`, `False`, truncated manifests,
missing manifests and malformed preferences. Cases cover both preferences,
five/six/unknown loaded acts and BIN/TXT launches. Five-act BIN games tolerate
unused optional-data errors, while requested TXT/loaded-six-act validation stays
strict. Files are preserved. The full startup/switching fixture also runs with GitHub LF line
endings against the original Windows manifest hashes; content edits still fail.
Startup-loader tests verify feature-specific rejection reasons,
initialization-stage timeouts, diagnostic exports and older-DLL fallback. A
BIN-only Act VI fixture skips the separate TXT package-authoring check.

The `generate-*.py` scripts regenerate native compatibility headers from a
matching executable. These tools require Python with `pefile` and `capstone`.
Keep the generated headers in source control: they are build inputs, unlike
compiled test executables and captured test output.
