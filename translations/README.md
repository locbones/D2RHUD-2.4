# HUD translations

The HUD includes English and machine-translated starters for German, European
and Latin American Spanish, French, Italian, Japanese, Korean, Polish, Brazilian
Portuguese, Russian, and Simplified and Traditional Chinese. Native-speaker
review is welcome. The packs cover HUD menus, tooltips, quest text, and map-event
briefings. Native game item names continue to use the game's translations.

## Players and translators

1. Open **HUD → Appearance → Language and translations**. Choose a language, or
   leave **Follow game language** selected. Restart after changing languages.
2. Choose **Open translation editor**. Open the relevant language JSON from
   `%LOCALAPPDATA%\D2RHUD\Translations` in that editor. It shows the English text
   beside the translation, with search, review flags, and format checks.
3. Edit the translation, mark reviewed entries, and export. Replace the matching
   JSON in that folder, then choose **Reload translations** in the HUD.

You can also edit a JSON file directly in a UTF-8 text editor. Keep the English
keys unchanged and edit the values under `strings`. Preserve `%s`, `%d`, `%u`,
`%.1f`, `%%`, and other format arguments exactly and in the same order. Use `\n`
for line breaks in JSON. Do not add `##` widget identifiers. Empty, invalid, or
missing entries fall back to bundled translations or English. A broken pack
does not prevent the game from starting.

Send the corrected JSON to the mod maintainer to share improvements. The
`reviewed` list identifies the entries you checked; it does not claim that the
entire machine-translated pack has been reviewed. `editor.html` works offline
and makes no network requests. It can merge an updated `enUS.json`, retaining
existing translations and adding new English entries for review.

Translations change presentation only. They do not change quest IDs, recipes,
rewards, companion files, progress validation, saved option keys, or hotkey IDs.

## Loading and fonts

Packs and a licensed Noto CJK fallback font are embedded in the DLL, so the
launcher does not need extra downloads. Optional distribution overrides live
beside the game in `D2RHUD/translations/<locale>.json`. Personal overrides in
`%LOCALAPPDATA%\D2RHUD\Translations/<locale>.json` take priority. Opening the
editor exports editable starter packs there without overwriting existing files.

The automatic setting reads a `-locale` command-line override, then Battle.net's
`LOCALE` registry preference (also used by Pandemonium), and falls back to
English. The HUD language override is stored separately in
`%LOCALAPPDATA%\D2RHUD\language.json`.

Localized UI uses Windows language fonts when available, with an embedded Noto
fallback. Glyphs are prepared at startup to bound atlas memory. Restart after
an edit introduces a new character that is absent from the current atlas.
The Noto font's license is in `d2rhud/fonts/NotoSansCJK-LICENSE.txt` and is
exported beside the editor. The bundled face comes from
[Noto CJK](https://github.com/notofonts/noto-cjk).

## Maintainers

From the repository root:

```powershell
python tools/hud-translations.py --extract --quests path/to/data/D2RHUD/quests.json
python tools/hud-translations.py --validate
```

Extraction updates the English source. Translators can merge it using the
offline editor. The optional `--translate` authoring command sends public
interface strings to Google Translate to seed **missing** entries. It never
runs inside the game, and preserves existing translations and review flags.
All non-English initial packs are machine-generated and require human review.
