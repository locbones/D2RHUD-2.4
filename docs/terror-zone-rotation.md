# Terror-zone rotation

The display and monster selectors use the same UTC-based rotation function.
Automatic selection advances once per full terror-duration plus break period,
retaining the random starting offset generated when the configuration loads:

`group = (elapsed complete periods + random starting offset) % group count`.

With a 60-minute duration and no break, the selected group advances hourly
during an ongoing session. Previously, both selectors used only the initial
offset, so the countdown could restart without changing the active group.
Manual selection still holds its chosen group. Existing date-window, break,
quest/difficulty and event exclusions remain in their original paths. Existing
monster callbacks use the new selection; this change does not retune monsters
that were already spawned. UTC boundaries avoid dependence on local time zones.

Source: `D2RHUD-terrorzone-rotation-source.zip`, SHA-256
`413b8e876a0cd492b24d3f87995b7ae0a7a6cce7d955bdef2ab31c5bb67301cd`.
The supplied `terrorzone_rotation.h` is integrated directly as
`plugin/D2RHUD/TerrorZoneRotation.h`, with the two call-site replacements from
`reference/source-integration.patch`. The header is unchanged (SHA-256
`c776df2ff8e63365d655ab83434902d21d9af8cab2cfdd29e46b6899f154751e`).
The archive's compiled DLLs, MASM adapters and binary reconstruction tools are
not build inputs. The project builds its normal complete HUD DLL from source.
No executable or `desecratedzones.json` edits are needed.

Run `tests\run-terror-zone-rotation.cmd <output-directory>` from an x64 Visual
Studio developer prompt. It covers exact hourly/phase boundaries, complete
wraparound, random offsets, single/empty groups and extreme timestamp bounds.
A live hourly transition remains an in-game acceptance check: use automatic
selection and compare the displayed group and newly spawned monsters across a
rotation boundary. A manual override deliberately holds its selected group.
