# Persistent Summons

Enable **Persistent Summons** in **D2RHUD Options**. The default is off. Changes
apply immediately and are saved as `PersistentSummons` in the active mod's HUD
configuration. Summon a companion and save normally to establish its roster;
companions lost before that save are not recorded.

On joining the next game, surviving permanent companions are recreated in town
after the character, equipment, skills and server client are ready. Skeletons
do not require fresh corpses. Permanent custom companions using the Hydra
implementation can return in town, one restored unit per saved unit. This does
not change normal casting restrictions. A crowded joining area is retried
every five simulation frames, slowing to once per second after 125 attempts;
successful placements are not repeated. Deferred placements remain in an early
save's companion roster and can return after the player moves into a free area.
Returning companions start in native neutral animation mode. This avoids
replaying fresh-cast HD spawn timelines whose hide/reveal events can be
interrupted during joining and leave an otherwise living summon invisible.
Normal casts keep their configured summon animation.

Eligibility uses actual pet ownership, the summoning skill, native monster AI,
and explicit temporary-summon markers. It is independent of character class,
custom skill ID or custom pet-type name. The supported native summon families
are Valkyrie (16), Skeleton/Mage (31), Shadow (49), Golem (56), Raven-style
creation (114), Vine (115), Druid companion (119), and Hydra-style creation
(144). Custom rows using these families are supported. A new native skill
implementation requires its own initialization audit.

Finite native AIs, timed-summon modifier 21 and trap expiration events are
excluded. A buff expiring on a permanent companion does not make it temporary.
Neither an implementation number nor a positive skill formula alone determines
lifetime: permanent custom monsters can reuse the Raven/Hydra creation code.
Iron Golem and item-consuming Iron Golem skills keep the original native path;
mercenaries also retain their normal handling.

Recreated companions use native stats, passive skills, equipment initialization
and AI. Current skill availability and native pet limits are checked, so a respec
or removed skill-granting item cannot retain an unavailable skill. Saved summon
levels are capped at the character's current available level. Skeleton element
and component appearance are preserved. Exact health, temporary buffs and exact
random equipment rolls are not serialized. This is a surviving-companion roster,
not a serialized copy of every live monster structure.

## Save compatibility and location

The native `.d2s` structure and serialized byte buffer are unchanged. Each
character instead has a versioned JSON companion file at:

```
%LOCALAPPDATA%\D2RHUD\Summons\<installation-key>\<mod-key>\<character-key>.json
```

Enabling the option creates the companion directory immediately; the character
JSON is created on a successful character save. Hover **Persistent Summons** to
see native feature availability, the resolved folder, the most recent saved
roster path, or the filesystem error and error number if creation/writing failed.
These diagnostics appear only in the tooltip. If character save/load hooks are
unavailable, the tooltip also reports the lifecycle dependency's failure.
The save/load hooks are checked independently of the Survivors avatar and skill
hooks. An unrelated Survivors compatibility failure does not disable persistence;
a mismatch in the character loader/writer or summon routines still does.

`%LOCALAPPDATA%\D2RHUD\feature-status.txt` is replaced at game startup with the
loaded HUD/game paths, HUD build time, and installation status of right-click
item conversion, Survivors, save/load hooks and summon persistence. Signature
failures include the first differing native function address. A signature
mismatch identifies changed code, without identifying which external patch or
module changed it.

The installation key hashes the normalized game-install directory. The mod
directory and character filename encode the mod/name and character creation
identity. Retail and a mod named `base` have separate namespaces. Each JSON
contains its readable character identity, creation time, class, mode flags,
native save checksum and save size. An identity/fingerprint mismatch, malformed
file, unsupported version or missing file is ignored. Writes use a flushed
temporary file followed by atomic replacement. A storage failure does not block
or alter the native character save.

Move the matching JSON with the character save when migrating installations.
The destination installation has a different installation-key directory, so
place the JSON under that installation/mod's directory. Restoring an older
`.d2s` without its matching JSON deliberately does not restore a newer army.
Playing and saving without HUD also invalidates an older roster's fingerprint.
Saving with no surviving companions, after death, or with the option off writes
an empty roster and prevents the previous army from returning.

For TCP/IP games, the authoritative host stores each player's companion file.
The host must enable the option and retain that file for later games. A joining
client's toggle does not override the host. Moving to another host requires
moving the matching companion file as well as the native character save.

## Verification

The implementation is gated to the audited executable described in
[the native audit](summon-persistence-native.md). Automated tests cover roster
validation, unchanged save bytes, survival/ownership checks, custom classes and
monsters, all supported creation families, skeleton components, temporary AI,
current skill limits, one-time restoration, deferred placement and disabling.
Existing CubeMap/Survivors native and UI regressions also pass.

Live gameplay and a second TCP/IP client remain necessary to confirm native
placement, AI, pet portraits and network synchronization. The manual check is:

1. Enable the option, summon permanent stock and custom companions, and let
   selected minions die or unsummon them. Include an Iron Golem and finite pets.
2. Save and exit, then rejoin. Confirm only surviving permanent companions
   return in town and the Iron Golem retains its original item properties.
3. Check combat, passive/aura effects, skeleton mage elements, pet portraits and
   party visibility. Rejoin repeatedly to verify counts never grow.
4. Remove a skill-granting item or respec, save, and verify unavailable skills
   and reduced limits are respected. Disable the option, save and rejoin to
   verify the old army does not return.
5. Repeat with a remote TCP/IP player while the host has the option enabled.
