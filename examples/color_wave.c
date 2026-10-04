#include <stdio.h>
#include "time.h"
#include "math.h"

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

#define w 512
#define h 256
static int pixels[w * h];

static int get_milliseconds() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int color(int r, int g, int b) {
    return (r << 16) | (g << 8) | b;
}

int main() {
    jl_init();
    jl_panel_t* panel = jl_open(NULL);

    while(1) {
        float t = get_milliseconds() / 500.0f;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float wave = (x + y) * 0.05f + t;

                int r = (sin(wave + 0.0f) + 1.0f) * 127.5f;
                int g = (sin(wave + 2.0f) + 1.0f) * 127.5f;
                int b = (sin(wave + 4.0f) + 1.0f) * 127.5f;

                pixels[x + y * w] = color(r, g, b); 
            }
        }

        jl_push_surface_async(panel, (const uint8_t*)pixels, w, h);

        Sleep(20);
    }

    jl_close(panel);
    return 0;
}
