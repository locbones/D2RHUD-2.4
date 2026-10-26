# Smooth Whirlwind audio

Edited versions of the existing Whirlwind wind recording, prepared for seamless
repetition. Both keep the original 44,100 Hz, 16-bit FLAC encoding and channel
counts (legacy mono, HD stereo).

The 350 ms cast lead-in is removed, and a 500 ms equal-power crossfade joins the
tail to the beginning. Circular filtering gently reduces rumble and bright
high frequencies. Gentle shared-channel volume leveling retains the swirl
without the original quiet-to-loud restart. The resulting loop is about
2.436 seconds, with no recurring silence or fade at its boundary.

## Install

Copy this folder's `data` directory into the active mod's MPQ directory, for
example:

```
Mods/RMD-MP/RMD-MP.mpq/data/global/sfx/skill/barbarian/whirlwind1.flac
Mods/RMD-MP/RMD-MP.mpq/data/hd/global/sfx/skill/barbarian/whirlwind1_hd.flac
```

Fully restart D2R to reload cached audio.
For another mod name, copy the two files into that mod's equivalent paths.
The audio files need to be distributed with the mod; they are separate from
`d2rhud.dll`.

These overrides replace the existing sound resources, so they also affect
ordinary Whirlwind and anything else using those recordings. The HD sound
and HD timeline sound both refer to the same HD file. This asset edit leaves
the sound table and native sound start/stop behavior as configured; the files
are prepared for looping playback rather than enabling that playback here.

The rebuild tool can generate local WAV previews and a measurements report.
These generated checks are excluded from Git; the two FLAC overrides above are
the distributable mod assets. In-game listening is needed to judge tone and volume.

## Rebuild

Use Python with `numpy` and `soundfile` installed:

```
python tools/whirlwind_audio.py --data-root "PATH/TO/ORIGINAL/D2R/data" --output examples/whirlwind-audio
```

Use the original extracted game data as input, not these already edited mod
overrides. Originals are read without modification. To undo the installed
override, remove these two files from the mod or restore any previous overrides.
