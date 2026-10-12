#include "../src/cupread/cupread.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "/home/claude/bcdec/stb_image_write.h"
#include <cstdio>
#include <chrono>
int main(int argc, char** argv) {
    std::string dir = argv[1];
    auto t0 = std::chrono::steady_clock::now();
    std::map<std::string, cupread::Frame> frames;
    auto r = cupread::loadSprites(dir + "/StreamingAssets/AssetBundles/atlas_slimelevel",
        {"lg_slime_idle_", "lg_slime_jump_", "lg_slime_air_up_", "lg_slime_air_down_", "lg_slime_up_down_trans_", "lg_slime_punch_", "lg_slime_hurt_", "lg_slime_death_", "lg_slime_dust_a_", "lg_slime_explode_"}, frames);
    auto t1 = std::chrono::steady_clock::now();
    printf("sprites ok=%d err=%s count=%zu in %lld ms\n", r.ok, r.error.c_str(), frames.size(),
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    for (auto& [n, f] : frames) {
        if (n == "lg_slime_dust_a_0005" || n == "lg_slime_jump_0004" || n == "lg_slime_punch_0010" || n == "lg_slime_idle_0001") {
            printf("%s %dx%d center %.1f,%.1f\n", n.c_str(), f.width, f.height, f.centerX, f.centerY);
            stbi_write_png(("out_" + n + ".png").c_str(), f.width, f.height, 4, f.rgba.data(), f.width * 4);
        }
    }
    std::map<std::string, std::vector<uint8_t>> clips;
    auto c = cupread::loadClips(dir, dir + "/StreamingAssets/AssetBundles/music_mus_slime",
        {"sfx_slime_big_jump_01", "sfx_slime_big_land_02", "sfx_slime_big_death_01"}, clips);
    printf("clips ok=%d err=%s count=%zu\n", c.ok, c.error.c_str(), clips.size());
    for (auto& [n, d] : clips) { printf("%s %zu bytes\n", n.c_str(), d.size()); FILE* fp = fopen(("out_" + n + ".fsb").c_str(), "wb"); fwrite(d.data(), 1, d.size(), fp); fclose(fp); }
}
