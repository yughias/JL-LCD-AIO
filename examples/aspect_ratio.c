#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

#define MAX_DIM 256
static uint32_t pixels[MAX_DIM * MAX_DIM];

static uint32_t color(int r, int g, int b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void render_aspect_guides(int w, int h) {
    int third_x1 = w / 3;
    int third_x2 = (w * 2) / 3;
    int third_y1 = h / 3;
    int third_y2 = (h * 2) / 3;

    int cx = w / 2;
    int cy = h / 2;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = x + y * w;

            // outer border
            if (x <= 1 || x >= w - 2 || y <= 1 || y >= h - 2) {
                pixels[i] = color(0, 230, 255);
                continue;
            }

            // crosshair
            if ((x == cx && abs(y - cy) <= 10) || (y == cy && abs(x - cx) <= 10)) {
                pixels[i] = color(255, 230, 0);
                continue;
            }

            // rule of thirds grid
            if (((x == third_x1 || x == third_x2) && (y % 4 < 2)) ||
                ((y == third_y1 || y == third_y2) && (x % 4 < 2))) {
                pixels[i] = color(60, 180, 90);
                continue;
            }

            // diagonals
            float norm_x = (float)x / (float)(w - 1);
            float norm_y = (float)y / (float)(h - 1);
            if (fabsf(norm_x - norm_y) < 0.006f || fabsf(norm_x - (1.0f - norm_y)) < 0.006f) {
                pixels[i] = color(140, 50, 110);
                continue;
            }

            // Background
            pixels[i] = color(28, 32, 42);
        }
    }
}

int main(void) {
    if (!jl_init()) return 1;

    jl_panel_t* panel = jl_open(NULL);
    if (!panel) return 1;

    printf("Sync Aspect Ratio Guide Example\n");
    printf("Examples: 16:9, 4:3, 1:1, 9:16, 21:9 (or 0:0 to quit)\n\n");

    while (1) {
        float aw = 0.0f, ah = 0.0f;
        printf("Enter aspect ratio (W:H): ");

        if (scanf("%f:%f", &aw, &ah) != 2) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            printf("Invalid format! Use W:H (e.g., 16:9)\n\n");
            continue;
        }

        if (aw <= 0.0f || ah <= 0.0f) {
            break;
        }

        float ratio = aw / ah;
        int w = MAX_DIM;
        int h = MAX_DIM;

        if (ratio >= 1.0f) {
            h = (int)(MAX_DIM / ratio);
        } else {
            w = (int)(MAX_DIM * ratio);
        }

        if (w < 8) w = 8;
        if (h < 8) h = 8;

        render_aspect_guides(w, h);

        jl_push_surface_sync(panel, (const uint8_t*)pixels, w, h);
        printf("-> Pushed %dx%d surface (%.2f:%.2f).\n\n", w, h, aw, ah);
    }

    jl_close(panel);
    return 0;
}
