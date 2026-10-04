#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <conio.h>

#define JL_PANEL_IMPLEMENTATION
#include "jl_lcd.h"

#define PANEL_W 960
#define PANEL_H 480

static uint32_t pixels[PANEL_W * PANEL_H];
static LARGE_INTEGER qpc_freq;

static inline uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static double get_time_ms(void) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)qpc_freq.QuadPart;
}

// Scene 0: Low Entropy (Clean Dashboard / Solid UI blocks)
// Scene 1: Medium Entropy (Trigonometric plasma + fine grid lines)
// Scene 2: High Entropy (Fine dither + pseudo-random texture)
static void render_scene(int scene_type, int frame_idx) {
    uint32_t rng = 0x9E3779B9u + (uint32_t)frame_idx * 1664525u;

    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_W; x++) {
            int i = x + y * PANEL_W;

            if (scene_type == 0) {
                if (x < 20 || x >= PANEL_W - 20 || y < 20 || y >= PANEL_H - 20) {
                    pixels[i] = color(0, 190, 255);
                } else if ((x / 120 + y / 120) % 2 == 0) {
                    pixels[i] = color(30, 36, 48);
                } else {
                    pixels[i] = color(20, 24, 32);
                }
            } 
            else if (scene_type == 1) {
                int bar = (frame_idx * 10) % PANEL_W;
                if (abs(x - bar) < 6) {
                    pixels[i] = color(255, 255, 255);
                } else if (x % 16 == 0 || y % 16 == 0) {
                    pixels[i] = color(220, 90, 40);
                } else {
                    uint8_t r = (uint8_t)((x * 255) / PANEL_W);
                    uint8_t g = (uint8_t)((y * 255) / PANEL_H);
                    uint8_t b = (uint8_t)(((x ^ y) + frame_idx * 4) & 0xFF);
                    pixels[i] = color(r, g, b);
                }
            } 
            else {
                rng = rng * 1664525u + 1013904223u;
                uint8_t noise = (uint8_t)(rng >> 24);
                uint8_t grid = ((x ^ y) & 1) ? 180 : 40;
                pixels[i] = color(
                    (uint8_t)((grid + noise) >> 1),
                    (uint8_t)(noise ^ (x & 0xFF)),
                    (uint8_t)(noise ^ (y & 0xFF))
                );
            }
        }
    }
}

static size_t probe_exact_jpeg_size(jl_panel_t* panel, float quality) {
    jl_set_jpeg_quality(panel, quality);

    size_t low = 1024;
    size_t high = JL_MAX_JPEG_BUFFER;
    size_t best = high + 1;

    while (low <= high) {
        size_t mid = low + (high - low) / 2;
        jl_set_max_transfer_size(panel, mid);

        if (jl_push_surface_sync(panel, (const uint8_t*)pixels, PANEL_W, PANEL_H)) {
            best = mid;
            if (mid <= 256) break;
            high = mid - 256;
        } else {
            low = mid + 256;
        }
    }
    return best;
}

static void run_matrix_benchmark(jl_panel_t* panel) {
    const float qualities[] = { 0.30f, 0.50f, 0.75f, 0.85f, 0.95f };
    const size_t limits_kb[] = { 32, 64, 80, 128 };
    const char* scene_names[] = {
        "Low Entropy (Flat UI / Dashboard)",
        "Medium Entropy (Gradients + Fine Grid)",
        "High Entropy (Noise + Dither Stress)"
    };

    for (int s = 0; s < 3; s++) {
        render_scene(s, 0);
        printf("\n========================================================================\n");
        printf(" SCENE %d: %s\n", s + 1, scene_names[s]);
        printf("========================================================================\n");
        printf(" Quality | Est. Size |  32 KB Limit |  64 KB Limit |  80 KB Limit | 128 KB Limit\n");
        printf("---------+-----------+--------------+--------------+--------------+-------------\n");

        for (int q = 0; q < 5; q++) {
            float qual = qualities[q];
            size_t exact_bytes = probe_exact_jpeg_size(panel, qual);

            if (exact_bytes > JL_MAX_JPEG_BUFFER) {
                printf("  %5.2f  |  >256 KB  |", qual);
            } else {
                printf("  %5.2f  | %6.1f KB |", qual, exact_bytes / 1024.0);
            }

            for (int l = 0; l < 4; l++) {
                jl_set_jpeg_quality(panel, qual);
                jl_set_max_transfer_size(panel, limits_kb[l] * 1024);

                const int runs = 5;
                int passed = 0;
                double t0 = get_time_ms();
                for (int r = 0; r < runs; r++) {
                    if (jl_push_surface_sync(panel, (const uint8_t*)pixels, PANEL_W, PANEL_H)) {
                        passed++;
                    }
                }
                double avg_ms = (get_time_ms() - t0) / runs;

                if (passed == runs) {
                    printf(" %5.1fms (%2.0ffps)|", avg_ms, 1000.0 / avg_ms);
                } else {
                    printf("   DROPPED    |");
                }
            }
            printf("\n");
        }
    }

    jl_set_jpeg_quality(panel, 0.75f);
    jl_set_max_transfer_size(panel, JL_DEFAULT_MAX_JPEG);
    printf("\nDefaults restored (quality = 0.75, max_transfer_size = 80 KB).\n\n");
}

static void run_live_tuner(jl_panel_t* panel) {
    float quality = 0.75f;
    size_t max_kb = 80;
    int scene = 1;
    int frame = 0;

    printf("\n--- Live Interactive Tuner ---\n");
    printf("Controls:\n");
    printf("  [W / S] : Increase / Decrease JPEG Quality (+/- 0.05)\n");
    printf("  [D / A] : Increase / Decrease Max Transfer Size (+/- 8 KB)\n");
    printf("  [1/2/3] : Switch Scene (1=Clean UI, 2=Grid/Plasma, 3=Noise)\n");
    printf("  [Q]     : Quit to menu\n\n");

    jl_set_jpeg_quality(panel, quality);
    jl_set_max_transfer_size(panel, max_kb * 1024);

    double accum_ms = 0.0;
    int count = 0;
    int passed = 0;

    while (1) {
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'q' || ch == 'Q') break;
            if (ch == 'w' || ch == 'W') quality = (quality + 0.05f > 1.0f) ? 1.0f : quality + 0.05f;
            if (ch == 's' || ch == 'S') quality = (quality - 0.05f < 0.05f) ? 0.05f : quality - 0.05f;
            if (ch == 'd' || ch == 'D') max_kb = (max_kb + 8 > 256) ? 256 : max_kb + 8;
            if (ch == 'a' || ch == 'A') max_kb = (max_kb > 8) ? max_kb - 8 : 8;
            if (ch == '1') scene = 0;
            if (ch == '2') scene = 1;
            if (ch == '3') scene = 2;

            jl_set_jpeg_quality(panel, quality);
            jl_set_max_transfer_size(panel, max_kb * 1024);
        }

        render_scene(scene, frame++);

        double t0 = get_time_ms();
        bool ok = jl_push_surface_sync(panel, (const uint8_t*)pixels, PANEL_W, PANEL_H);
        double dt = get_time_ms() - t0;

        accum_ms += dt;
        count++;
        if (ok) passed++;

        if (count >= 10) {
            double avg_ms = accum_ms / count;
            printf("\r[Scene %d] Quality: %4.2f | Limit: %3zu KB | Avg: %5.1f ms (%4.1f FPS) | Pass: %d/%d   ",
                   scene + 1, quality, max_kb, avg_ms, 1000.0 / avg_ms, passed, count);
            fflush(stdout);
            accum_ms = 0.0;
            count = 0;
            passed = 0;
        }
    }
    printf("\n\n");
}

int main(void) {
    QueryPerformanceFrequency(&qpc_freq);

    if (!jl_init()) {
        printf("Failed to initialize COM runtime.\n");
        return 1;
    }

    jl_panel_t* panel = jl_open(NULL);
    if (!panel) {
        printf("No JL LCD panel found.\n");
        return 1;
    }

    while (1) {
        printf("=== JL LCD Quality & Transfer Size Benchmark ===\n");
        printf("1. Run Full Matrix Benchmark (Quality x MaxTransferSize x Scene)\n");
        printf("2. Interactive Live Tuner (W/S Quality, A/D Max Size, 1-3 Scene)\n");
        printf("0. Quit\n");
        printf("Select option: ");

        int choice = -1;
        if (scanf("%d", &choice) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            continue;
        }

        if (choice == 0) break;
        if (choice == 1) run_matrix_benchmark(panel);
        if (choice == 2) run_live_tuner(panel);
    }

    jl_close(panel);
    return 0;
}
