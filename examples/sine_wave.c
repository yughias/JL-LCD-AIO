#include <stdio.h>
#include "time.h"
#include "math.h"

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

#define w 512
#define h 256
static int pixels[w * h];

static int color(int r, int g, int b) {
    return (r << 16) | (g << 8) | b;
}

int main() {
    jl_init();
    jl_panel_t* panel = jl_open(NULL);

    float off = 0;
    while(1) {
        off += 0.05;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int v = (sin(x / 20.0f + y / 20.0f + off) + 1) * 127.5f;
                pixels[x + y * w] = color(v, v, v);
            }
        }

        jl_push_surface_async(panel, (const uint8_t*)pixels, w, h);

        Sleep(20);
    }

    jl_close(panel);
    return 0;
}
