#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "tc001.h"

#ifdef _WIN32
#include <windows.h>   // for Sleep
#endif

static volatile sig_atomic_t running = 1;
static void on_sigint(int signo) { (void)signo; running = 0; }

/* ---------- ASCII heatmap helpers ---------- */

static const char* RAMP = " .:-=+*#%@";  // 10 levels
static uint8_t* g_u8 = NULL;
static int      g_u8_cap = 0;

static void ascii_preview(const uint8_t* img, int w, int h, int sx, int sy) {
    for (int y = 0; y < h; y += 2) {          // step 2 vertically
        for (int x = 0; x < w; ++x) {         // step 1 horizontally
            unsigned v = img[y * w + x];
            putchar(RAMP[(v * 9) / 255]);
        }
        putchar('\n');
    }
}

/* ---------- Frame callback ---------- */

static void on_frame(const tc001_frame* f, void* user) {
    (void)user;

    const int count = f->width * f->height;
    const uint16_t* px = (const uint16_t*)f->data;

    // min/max on raw U16
    uint16_t lo = 0xFFFF, hi = 0;
    for (int i = 0; i < count; ++i) {
        uint16_t v = px[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }

    // scratch for 8-bit preview
    if (g_u8_cap < count) {
        free(g_u8);
        g_u8 = (uint8_t*)malloc(count);
        g_u8_cap = count;
    }
    tc001_u16_to_u8(px, count, g_u8);  // simple AGC -> 0..255

    // clear console-ish (ANSI)
    printf("\x1b[H\x1b[2J");

    // show coarse preview (downsample to keep it readable)
    ascii_preview(g_u8, f->width, f->height, /*sx=*/2, /*sy=*/2);

    printf("\nframe: %dx%d fmt=%d  min=%u  max=%u\n",
           f->width, f->height, f->format, (unsigned)lo, (unsigned)hi);
    fflush(stdout);
}

int main(void) {
    signal(SIGINT, on_sigint);

    tc001_handle* h = NULL;
    char err[256] = {0};

    if (tc001_open(&h, 0, 0, err, sizeof err) != TC001_OK) {
        fprintf(stderr, "open failed: %s\n", err);
        return 1;
    }
    if (tc001_start(h, on_frame, NULL, err, sizeof err) != TC001_OK) {
        fprintf(stderr, "start failed: %s\n", err);
        tc001_close(h);
        return 1;
    }

    printf("Streaming… Ctrl+C to stop.\n");
    while (running) {
    #ifdef _WIN32
        Sleep(50);
    #else
        struct timespec ts = {0, 50*1000*1000}; nanosleep(&ts, NULL);
    #endif
    }

    tc001_stop(h);
    tc001_close(h);
    free(g_u8);
    return 0;
}
