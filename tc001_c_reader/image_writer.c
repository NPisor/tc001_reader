// image_writer.c

#include "image_writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <time.h>

// OpenCV for streaming/display
#include <opencv2/opencv.hpp>

// Raw sensor dimensions
#define RAW_W        256
#define RAW_H        192
#define PIXEL_COUNT  (RAW_W * RAW_H)
#define FRAME_SIZE   (PIXEL_COUNT * 2)

#define CLOCK_MONOTONIC 1 // Use monotonic clock for timing

// Output dimensions (rotated 90° CW)
#define IMAGE_WIDTH  RAW_H   // 192
#define IMAGE_HEIGHT RAW_W   // 256

// --------------------------------------------------------------------
// Simple OpenCV‐based streamer
// --------------------------------------------------------------------
static cv::VideoWriter _writer;
static bool            _writer_init = false;

static void stream_frame(const uint8_t* rgb)
{
    // wrap raw RGB into an OpenCV Mat (height, width, 3 channels)
    cv::Mat frame(IMAGE_HEIGHT,
                  IMAGE_WIDTH,
                  CV_8UC3,
                  const_cast<uint8_t*>(rgb));

    // lazily initialize VideoWriter (MJPG @ 25fps -> "stream.avi")
    if (! _writer_init) {
        int fourcc = cv::VideoWriter::fourcc('M','J','P','G');
        double fps = 25.0;
        _writer.open("stream.avi", fourcc, fps,
                     cv::Size(IMAGE_WIDTH, IMAGE_HEIGHT), true);
        if (! _writer.isOpened()) {
            fprintf(stderr, "⚠️  Could not open video writer\n");
        }
        _writer_init = true;
    }

    // write to file/stream
    if (_writer.isOpened()) {
        _writer.write(frame);
    }

    // display on screen
    cv::imshow("Thermal Camera", frame);
    cv::waitKey(1);  // needed to refresh the window
}

static void save_bmp(const char* fname, const uint8_t* rgb) {
    uint32_t row_bytes = IMAGE_WIDTH * 3;
    uint32_t pad_size  = (4 - (row_bytes % 4)) % 4;
    uint32_t data_size = (row_bytes + pad_size) * IMAGE_HEIGHT;
    uint32_t file_size = 54 + data_size;

    uint8_t file_hdr[14] = {
        'B','M',
        file_size & 0xFF, (file_size>>8)&0xFF,
        (file_size>>16)&0xFF, (file_size>>24)&0xFF,
        0,0, 0,0,
        54,0,0,0
    };
    uint8_t info_hdr[40] = {
        40,0,0,0,
        IMAGE_WIDTH & 0xFF, (IMAGE_WIDTH>>8)&0xFF,
        (IMAGE_WIDTH>>16)&0xFF, (IMAGE_WIDTH>>24)&0xFF,
        IMAGE_HEIGHT & 0xFF, (IMAGE_HEIGHT>>8)&0xFF,
        (IMAGE_HEIGHT>>16)&0xFF, (IMAGE_HEIGHT>>24)&0xFF,
        1,0, 24,0
    };

    FILE* f = fopen(fname, "wb");
    if (!f) return;
    fwrite(file_hdr, 1, 14, f);
    fwrite(info_hdr, 1, 40, f);

    uint8_t pad[3] = {0,0,0};
    for (int y = IMAGE_HEIGHT - 1; y >= 0; y--) {
        fwrite(rgb + (size_t)y * row_bytes, 1, row_bytes, f);
        fwrite(pad, 1, pad_size, f);
    }
    fclose(f);
}

void process_frame(const uint8_t* raw, int size) {
    // 1) Read raw 16-bit pixels
    uint16_t* pixels = malloc(PIXEL_COUNT * sizeof(uint16_t));
    if (!pixels) return;
    int count = size/2;
    if (count > PIXEL_COUNT) count = PIXEL_COUNT;
    for (int i = 0; i < count; i++) {
        pixels[i] = raw[2*i] | (raw[2*i+1] << 8);
    }

    // 2) Find min/max
    uint16_t mn = UINT16_MAX, mx = 0;
    for (int i = 0; i < count; i++) {
        if (pixels[i] < mn) mn = pixels[i];
        if (pixels[i] > mx) mx = pixels[i];
    }
    int flat = (mn == mx);

    // 3) Build grayscale RGB (raw orientation)
    uint8_t* rgb_raw = malloc(PIXEL_COUNT * 3);
    if (!rgb_raw) { free(pixels); return; }
    for (int i = 0; i < count; i++) {
        uint8_t g = flat
                  ? (uint8_t)(pixels[i] & 0xFF)
                  : (uint8_t)(255.0 * (pixels[i] - mn) / (mx - mn));
        rgb_raw[3*i + 0] = g;
        rgb_raw[3*i + 1] = g;
        rgb_raw[3*i + 2] = g;
    }
    free(pixels);

    // 4) Allocate rotated buffer (rotated 90° CW)
    uint8_t* rgb_rot = malloc(PIXEL_COUNT * 3);
    if (!rgb_rot) { free(rgb_raw); return; }

    // 5) Rotate: raw(r,c) -> rot(c, RAW_H-1-r)
    for (int r = 0; r < RAW_H; r++) {
        for (int c = 0; c < RAW_W; c++) {
            int src_i = r * RAW_W + c;
            int dst_row = c;
            int dst_col = (RAW_H - 1) - r;
            int dst_i = dst_row * IMAGE_WIDTH + dst_col;
            memcpy(&rgb_rot[3*dst_i], &rgb_raw[3*src_i], 3);
        }
    }
    free(rgb_raw);

    // 6) Stream instead of saving BMP
    static int idx = 0;
    if (flat) {
        fprintf(stderr, "⚠️ Frame %d is flat (min=%u max=%u)\n", idx, mn, mx);
    } else {
        printf("Frame %d: min=%u max=%u\n", idx, mn, mx);
    }
    stream_frame(rgb_rot);
    idx++;

    free(rgb_rot);
}
