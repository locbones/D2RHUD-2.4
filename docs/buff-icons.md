# Buff icon table loading

Buff Icons accepts `skills`, `skilldesc` and `states` tables as source `.txt`
files or compiled D2R 2.4 `.bin` files. For each table, the selected mod's data
folder takes precedence over the installation's loose data and CASC. Within
that folder TXT is preferred; if it is absent, BIN is used. A compiled mod table
therefore takes precedence over retail source text.

The compiled reader preserves skill/state IDs, curse exclusions, skill names,
localized description names, icon frames and class sprite selection. All eight
source/compiled combinations are supported. When source skills need links into
compiled states or descriptions, a matching `skills.bin` must accompany
`skills.txt` in the same data folder; compiled tables do not retain those
symbolic names. Malformed or unsupported binary layouts appear in the existing
Buff Icons status instead of being interpreted as another table format.

Source state `*ID` columns are comments: the catalogue uses actual compiled row
IDs. Empty source icon fields use their native compiled default of zero.

The fallback reads existing assets only. It does not create tables or change
character saves. The buff icon options, per-character selections and timers
retain their existing configuration format.
