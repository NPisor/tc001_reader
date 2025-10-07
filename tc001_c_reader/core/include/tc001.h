#pragma once
#include <stdint.h>
#include <stddef.h>

/* Export rules:
   - When building the DLL:   TC001_BUILD_DLL -> __declspec(dllexport)
   - When building static:    TC001_STATIC    -> (no decoration)
   - Otherwise (consumers):   (no decoration) to avoid dllimport noise in MSVC
*/
#if defined(_WIN32) || defined(_WIN64)
  #if defined(TC001_BUILD_DLL)
    #define TC001_API __declspec(dllexport)
  #elif defined(TC001_STATIC)
    #define TC001_API
  #else
    #define TC001_API
  #endif
#else
  #define TC001_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Core forward types ===== */
typedef struct tc001_handle tc001_handle;

/* ===== Basic streaming API ===== */
typedef enum {
  TC001_OK = 0,
  TC001_ERR_PARAM   = -1,
  TC001_ERR_NO_DEV  = -2,
  TC001_ERR_USB     = -3,
  TC001_ERR_ALLOC   = -4,
  TC001_ERR_STATE   = -5,
  TC001_ERR_INTERNAL= -6
} tc001_status;

typedef enum { TC001_FMT_U8 = 0, TC001_FMT_U16 = 1 } tc001_format;

typedef struct {
  int width;
  int height;
  int stride;               /* bytes per row */
  int64_t timestamp_ns;     /* 0 if unknown */
  tc001_format format;
  const uint8_t* data;      /* points to lib-owned buffer; copy if you need to keep it */
} tc001_frame;

typedef void (*tc001_frame_cb)(const tc001_frame* f, void* user);

/* ===== Packed/fusion types (must come BEFORE APIs that use them) ===== */
#pragma pack(push, 1)

typedef struct { float m[9];  } tc001_mat3;   /* row-major 3x3 */
typedef struct { float m[16]; } tc001_mat4;   /* row-major 4x4 */

typedef struct {
  uint64_t ts_ns;
  uint32_t frame_id;
  uint16_t width, height;
  uint16_t stride_bytes;
  uint8_t  pixel_format;    /* 0 = U16_LE */
  uint8_t  _pad0[3];
} tc001_frame_info;

typedef struct {
  uint16_t raw_min, raw_max;
  uint16_t p10_raw, median_raw, p90_raw;
  uint32_t bad_pixel_count;
  uint32_t hist256[256];    /* histogram of the 8-bit preview */
} tc001_frame_stats;

typedef struct {
  uint16_t w, h;            /* e.g., 64x48 */
  uint16_t pitch;           /* bytes per row (== w if tightly packed) */
  /* followed by w*h bytes (0..255), row-major */
} tc001_thumbnail8;

typedef struct {
  uint8_t  has_temp;        /* 0/1 */
  float    emissivity;      /* if known */
  float    ambient_C;       /* if known */
  float    gain_K_per_raw;  /* Kelvin = gain * raw + offset */
  float    offset_K;        /* Kelvin offset */
  uint32_t model_id;        /* identify LUT/model if needed */
} tc001_temp_model;

typedef struct {
  uint64_t calib_hash;      /* hash for change detection */
  tc001_mat3 K_therm;       /* set identity if unused */
  tc001_mat3 K_rgb;         /* set identity if unused */
  tc001_mat4 T_therm_rgb;   /* therm->rgb (R|t), identity if unused */
  float     H_therm_to_rgb[9]; /* optional homography; 0 if unused */
} tc001_calibration;

typedef struct {
  tc001_frame_info  info;
  tc001_frame_stats stats;
  tc001_temp_model  temp;
  tc001_calibration calib;
  /* Offsets (from start of this struct) to variable-length blobs: */
  uint32_t off_raw_u16;     /* width * height * 2 */
  uint32_t off_thumb_u8;    /* sizeof(tc001_thumbnail8) + w*h */
  uint32_t total_bytes;     /* total size of the packed payload */
} tc001_payload_hdr;

#pragma pack(pop)

/* ===== Device control / streaming ===== */
TC001_API tc001_status tc001_open(tc001_handle** out,
                                  uint16_t vid, uint16_t pid,
                                  char* err, size_t errcap);

TC001_API void         tc001_close(tc001_handle* h);

TC001_API tc001_status tc001_start(tc001_handle* h,
                                   tc001_frame_cb cb, void* user,
                                   char* err, size_t errcap);

TC001_API void         tc001_stop(tc001_handle* h);

TC001_API void         tc001_get_frame_dims(tc001_handle* h, int* w, int* hgt);

TC001_API void         tc001_u16_to_u8(const uint16_t* in, int count, uint8_t* out);

/* ===== Fusion payload helpers (exported!) ===== */
TC001_API size_t tc001_max_payload_bytes(int w, int h, int thumb_w, int thumb_h);

TC001_API size_t tc001_pack_payload(tc001_handle* h,
                                    void* dst, size_t dst_cap,
                                    int thumb_w, int thumb_h,
                                    int use_agc);

TC001_API void   tc001_set_calibration(tc001_handle* h, const tc001_calibration* c);

TC001_API void   tc001_set_temp_model(tc001_handle* h, const tc001_temp_model* t);

#ifdef __cplusplus
} /* extern "C" */
#endif
