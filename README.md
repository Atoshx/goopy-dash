# Goopy Dash

A Geometry Dash × Cuphead mashup. Goopy Le Grande, Cuphead's first boss, bounces into your Geometry Dash levels:
he hops along ahead of you, slams down to send slime dust rolling at your feet, and stretches out his boxing glove to punch.
Jump over it all and reach the end of the level to beat him.

- **Host game:** Geometry Dash 2.2081 (Steam, Windows), modded with Geode.
- **Companion game:** Cuphead (Steam, Windows). Goopy's animations and sounds are read from the player's own Cuphead install while the game runs; no Cuphead files are in this repository or its releases.
- **Players:** single player.

## How it works
- `src/cupread` reads Cuphead's Unity 2017.4 files: the `atlas_slimelevel` sprite atlas (BC7 textures, tight-packed sprite meshes) and the slime level's AudioClips (FSB5, played through GD's own FMOD).
- `src/Goopy.cpp` drives Goopy in `PlayLayer`: enter, idle, slam (dust wave hazard), stretch punch (glove hazard), defeated on level complete.
- Behaviour comes from the design sheets in `sheets/`; `tools/gen_sheets.py` generates `src/generated/Sheets.hpp` and `tools/preflight.py` checks every cell and cross-reference.
- Cuphead's folder comes from `GOOPYDASH_CUPHEAD_DIR` (Melty sets it), the mod's *Cuphead folder* setting, or the Steam libraries.

## Building
GitHub Actions builds the `.geode` with `geode-sdk/build-geode-mod` and packages it with the Geode loader for Melty.

## Credits
- Mod: DLG. Goopy Le Grande and Cuphead © StudioMDHR (read from your own copy). Geometry Dash © RobTop Games.
- Geode mod loader (Boost Software License 1.0), LZ4 (BSD-2-Clause), bcdec (MIT).
