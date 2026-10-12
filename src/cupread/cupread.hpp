// cupread: reads Cuphead's own Unity 2017.4 files from the player's install.
// Portable C++17, no dependencies beyond lz4 and bcdec (bundled source).
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cupread {

// One decoded sprite frame, RGBA8, rows top-to-bottom.
struct Frame {
    std::string name;
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    // Centre of the trimmed image relative to the sprite pivot, in source pixels (y up).
    float centerX = 0, centerY = 0;
};

struct LoadResult {
    bool ok = false;
    std::string error;
};

// Reads every sprite whose name starts with one of `prefixes` from a Unity
// AssetBundle containing a SpriteAtlas (e.g. atlas_slimelevel).
LoadResult loadSprites(std::filesystem::path const& bundlePath,
                       std::vector<std::string> const& prefixes,
                       std::map<std::string, Frame>& out);

// Reads the raw FSB5 bytes of the named AudioClips from Cuphead_Data.
// `typeSourceBundle` is any Cuphead bundle that carries the AudioClip type tree
// (the player-data .assets files ship without one).
LoadResult loadClips(std::filesystem::path const& cupheadDataDir,
                     std::filesystem::path const& typeSourceBundle,
                     std::vector<std::string> const& names,
                     std::map<std::string, std::vector<uint8_t>>& out);

} // namespace cupread
