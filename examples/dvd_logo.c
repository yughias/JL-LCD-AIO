#include <stdio.h>
#include <stdint.h>

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

#define w 960
#define h 480
static uint32_t pixels[w * h];

static uint32_t color(int r, int g, int b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static const char* dvd_art[5] = {
    "1100100010110",
    "1010100010101",
    "1010100010101",
    "1010010100101",
    "1100001000110"
};

int main(void) {
    jl_init();
    jl_panel_t* panel = jl_open(NULL);

    int bx = 40, by = 80;
    int bw = 58, bh = 28;
    int vx = 3,  vy = 3;

    uint32_t palette[] = {
        color(255, 50, 50),
        color(50, 255, 50),
        color(50, 150, 255),
        color(255, 255, 50),
        color(255, 50, 255),
        color(50, 255, 255)
    };
    int c_idx = 0;

    while (1) {
        bx += vx;
        by += vy;

        if (bx <= 0 || bx + bw >= w) { vx = -vx; c_idx = (c_idx + 1) % 6; }
        if (by <= 0 || by + bh >= h) { vy = -vy; c_idx = (c_idx + 1) % 6; }

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                    int lx = (x - bx - 3) / 4;
                    int ly = (y - by - 4) / 4;

                    if (lx >= 0 && lx < 13 && ly >= 0 && ly < 5 && dvd_art[ly][lx] == '1') {
                        pixels[x + y * w] = color(255, 255, 255);
                    } else {
                        pixels[x + y * w] = palette[c_idx];
                    }
                } else {
                    pixels[x + y * w] = color(0, 0, 0);
                }
            }
        }

        jl_push_surface_async(panel, (const uint8_t*)pixels, w, h);
        Sleep(20);
    }

    jl_close(panel);
    return 0;
}
