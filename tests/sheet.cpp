#include "../src/cupread/cupread.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "/home/claude/bcdec/stb_image_write.h"
#include <cstdio>
#include <cmath>
// contact sheet: frames placed on a common canvas by their pivot-relative centers
int main(int argc, char** argv) {
    std::map<std::string, cupread::Frame> fr;
    cupread::loadSprites(std::string(argv[1]) + "/StreamingAssets/AssetBundles/atlas_slimelevel", {argv[2]}, fr);
    int cellW = 700, cellH = 600, cols = 6; int n = fr.size(); int rows = (n + cols - 1) / cols;
    std::vector<uint8_t> img(cellW * cols * cellH * rows * 4, 0);
    for (size_t i = 0; i < img.size(); i += 4) { img[i] = 70; img[i+1] = 70; img[i+2] = 100; img[i+3] = 255; }
    int k = 0;
    for (auto& [name, f] : fr) {
        int cx = (k % cols) * cellW + cellW / 2, cy = (k / cols) * cellH + cellH / 2; k++;
        int x0 = cx + (int)std::lround(f.centerX - f.width / 2.0), y0 = cy - (int)std::lround(f.centerY + f.height / 2.0);
        for (int y = 0; y < f.height; y++) for (int x = 0; x < f.width; x++) {
            int X = x0 + x, Y = y0 + y; if (X < 0 || Y < 0 || X >= cellW * cols || Y >= cellH * rows) continue;
            uint8_t* s = &f.rgba[(y * f.width + x) * 4]; uint8_t* d = &img[(Y * cellW * cols + X) * 4];
            float a = s[3] / 255.f; for (int c = 0; c < 3; c++) d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a));
        }
        for (int x = -6; x <= 6; x++) { uint8_t* d = &img[(cy * cellW * cols + cx + x) * 4]; d[0] = 255; d[1] = d[2] = 0; }
    }
    stbi_write_png(argv[3], cellW * cols, cellH * rows, 4, img.data(), cellW * cols * 4);
    printf("%d frames\n", n);
}
