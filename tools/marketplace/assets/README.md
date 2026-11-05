# Marketplace button

`marketplace-button-source.png` was generated with the built-in image-generation
tool, using the existing RMD-MP gold-and-stone portal button as a style reference.
The new emblem uses merchant scales and coins on a burgundy inset.
The exact generation prompt and tool metadata are recorded in `generation.json`.

Prompt: “Create a production UI bitmap asset for a Diablo II Resurrected dark
fantasy mod: one square marketplace button, no text or letters. Match the chunky
aged gold square frame, engraved corner flourishes and layered dark iron/stone
inset of the reference. Replace the portal with a readable antique golden
balance scale above gold coins. Straight-on UI artwork, subdued dark fantasy
palette, weathered metal, crisp contours, transparent outside the frame.”

Run `python tools/marketplace/build-native-assets.py` with ImageMagick installed
to compile the HD and low-end SpA1 atlases. The native widget chooses normal,
pressed and hover states. Only the compiled sprites ship to player installations;
the source PNG and build tools remain developer assets.

The community gift emblem is authored in `MarketplaceGift.svg`. The same builder
compiles it to HD and low-end three-state SpA1 atlases; its source stays in the
developer repository. The native gift button uses the game’s gold font and
shadow controls, with no overlay window.
