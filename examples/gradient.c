#include <stdio.h>
#include "time.h"
#include "math.h"

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

#define w 256
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
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                unsigned char g = (sin(get_milliseconds() / 1000.0) + 1) / 2 * 255;
                pixels[x + y * w] = color(x, g, y);
            }
        }

        jl_push_surface_async(panel, (const uint8_t*)pixels, w, h);

        Sleep(20);
    }

    jl_close(panel);
    return 0;
}
