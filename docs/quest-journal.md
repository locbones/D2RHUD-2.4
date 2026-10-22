# Character quest journal

The quest system is enabled only for the **RMD-MP** mod (case-insensitive exact
mod name). Retail, other mods and differently named RMD copies keep their native
quest recipes; the HUD journal stays hidden and creates no quest save session.

The journal uses RMD-MP's actual quest-log stone panels, red tabs, engraved
title plates, gold-trimmed buttons and native close control. Artwork is loaded
once through the existing asset loader, not during journal drawing.

The Pandemonium side menu has a **Quest Journal** for the existing 45 tutorial
quests. Start at Quest 1, or explicitly import the progress represented by a
carried old Evil Barrier charm. Import is offered once for a fresh journal,
marks earlier rewards as already claimed and grants nothing. A transferred
charm cannot prove which character earned its old progress.

Choose **Start at Quest 1** or the explicit import once when setting up the
journal. Successful crafting evidence is retained while that choice is pending,
then applied when starting the journal.

Progress belongs to the character, independently of bags, statues and proof
parchments. Moving those items does not reset completed objectives. Evidence of
an observed acquisition or crafting activity can satisfy a later lesson once
earlier lessons are complete. Old proof-producing and ordinary turn-in recipes
are disabled by the server's isolated cube matcher, without rewriting the mod's
compiled recipe table or player item progress stats.

## Activities

- Kills count confirmed hostile, killable monster deaths, including kills
  credited to the player's pets. Already dead, friendly and revived units do
  not count twice. Countess plus a successful Thul deposit are separate facts.
- Finding lessons observe items owned by the character, including inventory,
  cube and personal stash. They do not consume the required item. Like the
  original proof recipes, they identify the item rather than proving its drop
  source; a traded item also qualifies. Quest 41 uses the custom `Gu1` GoldUp
  item, not ordinary gold currency.
- Crafting lessons observe successfully completed ordinary cube transactions:
  bag deposit/withdrawal, Daylight forging, amulet socketing, belt enhancement,
  Safety ring, Blood Contract, dye, jewel upconversion, Codex and Limit Break.
  Failed or partial output placement grants no crafting evidence. A successful
  recipe may return unused quantities from an input stack to the cube; that
  normal remainder step does not prevent completion. A successful mixed-rune
  helper deposit also records Thul.
- Dropping an equipped weapon and placing a Mythos Statue in the cube are
  observed directly. Weapon tracking continues while holding the equipped
  weapon on the cursor, and completes when it reaches the ground. Putting it
  into storage or transferring it ends that action. No extra Transmute is
  necessary for those lessons.
- Recipe/Rift Shop, mercenary information, Mod Info, Grail and Loot Filter
  lessons record menu visits. Both recipe and shop visits are required for
  Quest 10. Credits and the external Discord guide can be explicitly
  acknowledged in the journal after reading them.

Quests without a reward advance automatically. The six fixed stat bonuses are
claimed in the journal while carrying exactly one appropriate Evil Barrier
charm in inventory. Native saved modifiers preserve the original bonuses and
update the charm/player stats. Claims are recorded once per character.

The three actual charm upgrades (15, 30, 45) still use the cube: charm + Quill of
Quality + a weapon, armor, ring, amulet or charm for the chosen reward. The
journal lists all five choices. The Quill and chosen ingredient are consumed;
the upgraded charm retains its original modifiers. No proof is required.

## Installation and saves

Install the matching DLL and `examples/quests.json` as
`<mod>.mpq/data/D2RHUD/quests.json`. TCP/IP hosts and clients need the same quest
definitions and mod tables. Progress is authoritative on the host; journals for
TCP/IP characters are stored on that host. Offline journals are stored locally.
Supported native signatures are checked before enabling the feature. The
feature report includes quest hook status and the actual companion folder.

BIN-only distributions need `cubemain.bin`, `D2RHUD/quests.json` and the
matching `D2RHUD/table-metadata.json`. The companion retains recipe descriptions
which the game does not store in the BIN. Ordinary launches use the companion
bound to the compiled table even if old TXT files remain; `-txt` launches use
the source being compiled. The skull/Thul deposits, skull/Amn withdrawals,
Safety rings and jewel upconversion resolve their current activity recipes
instead of trusting the old exported row numbers. Correcting those legacy
bindings preserves the existing journal identity and claimed rewards.

`%LOCALAPPDATA%/D2RHUD/quest-events.log` records each observed ordinary cube
transaction's recipe row, input codes, native outcome and journal/session
status. Rejected or incomplete transactions never grant evidence. The log is
bounded to roughly 1 MiB and is written only on cube operations.

Quest data is stored below:

```text
%LOCALAPPDATA%/D2RHUD/Quests/<installation>/<mod>/<character identity>.json
```

Character identity uses name, class and relevant save flags. Header offset
`0x2c` is excluded: the game clears that timestamp on the first native save of
a new character. The old JSON `created` field is retained at zero for format
compatibility. Empty journals written with the old timestamp key migrate
automatically; the original companion remains intact. A journal with prior
evidence or rewards still requires a matching save checkpoint to migrate.
The `.d2s` structure and its existing size limit are unchanged. The journal is
written atomically when native character serialization succeeds, with up to
32 checkpoints matched to character save checksum/size. Back up and transfer
the journal alongside the `.d2s`. An unmatched, corrupt or incompatible journal
blocks further quest transactions rather than silently resetting rewards.
An incomplete milestone output transaction also blocks further claims.

A journal which has never been started and has **no activity in any saved
checkpoint** can recover automatically after definition or save-checkpoint
drift. The original file is first copied and verified byte for byte as
`<character>.json.unused-<old definition>-<content hash>.bak`. The recovered
journal retains its character identity, adds the current save checkpoint and
offers Start at Quest 1/import again. Any prior setup choice, completed quest,
reward readiness, evidence, kill count, menu visit, Countess/Thul observation
or uncertain transaction prevents this recovery. Used journals still require
matching data/backups, and their progress is never silently discarded.

`tools/generate-quest-journal.py` exports the original tasks, tips and reward
choices from `override_rules.lua`, plus explicit event definitions derived from
the mod tables. Display-only text edits retain journal compatibility; activity
binding/reward recipe changes alter the definition identity. The exporter
requires `--excel-root`, `--filter-path` and `--output`.

`tools/migrate-quest-tooltips.py <override_rules.lua>` changes the 45 old quest
and 10 proof tooltips to direct players to the journal. It backs up the file and
preserves original lesson/choice strings in Lua comments for future exports.
No unrelated filter rules, native character saves, launcher or game executable
are edited.

Native evidence and limits are documented in `quest-native-audit.md`. Automated
tests cover model/lifecycle persistence, actual native kill/cube/reward paths,
and all 45 journal views with the native font/panel art at multiple resolutions.
Live game acceptance remains a separate check.
