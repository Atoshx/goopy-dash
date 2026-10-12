#include "../src/cupread/cupread.hpp"
#include <cstdio>
int main(int c, char** v) {
    std::map<std::string, cupread::Frame> fr;
    cupread::loadSprites(std::string(v[1]) + "/StreamingAssets/AssetBundles/atlas_slimelevel", {"lg_slime_punch_", "lg_slime_idle_", "lg_slime_dust_a_", "lg_slime_jump_", "lg_slime_air_"}, fr);
    for (auto& [n, f] : fr) {
        // red glove pixels: r>150, g<60, b<60
        int minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9, cnt = 0, oy0 = 1e9;
        for (int y = 0; y < f.height; y++) for (int x = 0; x < f.width; x++) {
            auto* p = &f.rgba[(y * f.width + x) * 4];
            if (p[3] > 128 && y < oy0) oy0 = y;
            if (p[3] > 128 && p[0] > 140 && p[1] < 70 && p[2] < 70) { cnt++; minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
        }
        float left = f.centerX - f.width / 2.f, right = f.centerX + f.width / 2.f, bottom = f.centerY - f.height / 2.f, top = f.centerY + f.height / 2.f;
        printf("%-28s L%7.1f R%7.1f B%7.1f T%7.1f", n.c_str(), left, right, bottom, top);
        if (cnt > 500) printf("  glove x[%6.1f,%6.1f] y[%6.1f,%6.1f]", left + minx, left + maxx, top - maxy, top - miny);
        printf("\n");
    }
}
