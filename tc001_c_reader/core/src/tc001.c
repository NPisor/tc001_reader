#include "tc001.h"
#include <libusb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <stddef.h>
#if defined(_MSC_VER)
  #include <windows.h>
  typedef LONG tc001_atomic_int;
  #define TC001_ATOMIC_LOAD(p)   InterlockedCompareExchange((p), 0, 0)
  #define TC001_ATOMIC_STORE(p,v) InterlockedExchange((p), (LONG)(v))
#else
  #include <stdatomic.h>
  typedef _Atomic int tc001_atomic_int;
  #define TC001_ATOMIC_LOAD(p)   atomic_load((p))
  #define TC001_ATOMIC_STORE(p,v) atomic_store((p), (v))
#endif



/* === Device/stream constants from your reader.c === */
#define DEF_VENDOR_ID        0x0BDA
#define DEF_PRODUCT_ID       0x5830
#define INTERFACE_NUMBER     1
#define ISO_ENDPOINT         0x81
#define PACKET_SIZE          3072
#define NUM_PACKETS          64
#define TIMEOUT_MS           1000

#define FRAME_WIDTH   256
#define FRAME_HEIGHT  192
#define PIXEL_SIZE    2
#define FRAME_SIZE    (FRAME_WIDTH * FRAME_HEIGHT * PIXEL_SIZE)

struct tc001_handle {
  libusb_context* ctx;
  libusb_device_handle* dev;
  struct libusb_transfer* xfer;
  uint8_t* iso_buf;
  uint8_t* frame_buf;
  int      frame_pos;

  tc001_atomic_int running;
  tc001_frame_cb cb;
  void* cb_user;

#ifdef _WIN32
  HANDLE thread;
#else
  pthread_t thread;
#endif
};

static void seterr(char* out_buf, size_t out_cap, const char* msg) {
    if (!out_buf || out_cap == 0) return;
    if (!msg) { out_buf[0] = '\0'; return; }

    /* portable, truncates safely */
#if defined(_MSC_VER)
    /* MSVC's secure variant */
    _snprintf_s(out_buf, out_cap, _TRUNCATE, "%s", msg);
#else
    /* C11 / POSIX */
    snprintf(out_buf, out_cap, "%s", msg);
#endif
}

static void fill_tc001_frame(struct tc001_handle* h, tc001_frame* f) {
  f->width  = FRAME_WIDTH;
  f->height = FRAME_HEIGHT;
  f->stride = FRAME_WIDTH * PIXEL_SIZE;
  f->timestamp_ns = 0;      /* can fill with clock if you wish */
  f->format = TC001_FMT_U16;
  f->data   = h->frame_buf;
}

/* ===== Control sequence from your reader.c ===== */
static int send_standard_set_configuration(libusb_device_handle* dev) {
  unsigned char cfg[] = {
    0x1c,0x00,0x90,0x05,0x9a,0xab,0x83,0xe2,
    0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x01,0x00,0x0d,0x00,0x00,0x02,0x08,
    0x00,0x00,0x00,0x00,0x00,0x09,0x01,0x00,
    0x00,0x00,0x00,0x00
  };
  return libusb_control_transfer(
    dev,
    LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT,
    0x09, /* SET_CONFIGURATION */
    0x0001,
    0x0000,
    cfg, sizeof(cfg),
    TIMEOUT_MS
  );
}

static int send_vendor_setup(libusb_device_handle* dev) {
  unsigned char vs[] = { 0x05,0x84,0x00,0x00,0x00,0x00,0x00,0x08 };
  return libusb_control_transfer(
    dev,
    LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
    0x45,
    0x0078,
    0x1d00,
    vs, sizeof(vs),
    TIMEOUT_MS
  );
}

static int send_probe(libusb_device_handle* dev) {
  unsigned char probe[] = {
    0x01,0x00,0x01,0x02,0x80,0x1a,0x06,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,
    0x00,0x00,0x80,0x01,0x00,0x00,0x0c,0x00,0x00
  };
  return libusb_control_transfer(
    dev,
    LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
    0x01,             /* SET_CUR */
    0x0100,           /* VS_PROBE_CONTROL */
    INTERFACE_NUMBER,
    probe, sizeof(probe),
    TIMEOUT_MS
  );
}

static int send_commit(libusb_device_handle* dev) {
  unsigned char commit[] = {
    0x01,0x00,0x01,0x02,0x80,0x1a,0x06,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x20,
    0x00,0x00,0x00,0x03,0x00,0x00,0x0c,0x00,0x00
  };
  return libusb_control_transfer(
    dev,
    LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT,
    0x01,             /* SET_CUR */
    0x0200,           /* VS_COMMIT_CONTROL */
    INTERFACE_NUMBER,
    commit, sizeof(commit),
    TIMEOUT_MS
  );
}

/* ===== ISO callback ===== */
static void LIBUSB_CALL iso_cb(struct libusb_transfer* t) {
  struct tc001_handle* h = (struct tc001_handle*)t->user_data;

  if (t->status == LIBUSB_TRANSFER_COMPLETED) {
    for (int i = 0; i < t->num_iso_packets; i++) {
      struct libusb_iso_packet_descriptor* d = &t->iso_packet_desc[i];
      if (d->status != LIBUSB_TRANSFER_COMPLETED || d->actual_length < 2) continue;

      uint8_t* data    = libusb_get_iso_packet_buffer_simple(t, i);
      uint8_t  hdr_len = data[0];
      uint8_t  flags   = data[1];
      int      payload = d->actual_length - hdr_len;

      if (payload > 0 && h->frame_pos + payload <= FRAME_SIZE) {
        memcpy(h->frame_buf + h->frame_pos, data + hdr_len, payload);
        h->frame_pos += payload;
      }

      if (flags & 2) { /* EOF */
        if (h->frame_pos >= FRAME_SIZE && h->cb) {
          tc001_frame f; fill_tc001_frame(h, &f);
          h->cb(&f, h->cb_user);
        }
        h->frame_pos = 0;
      }
    }
  }

  if (TC001_ATOMIC_LOAD(&h->running)) {
    if (libusb_submit_transfer(t) < 0) {
      TC001_ATOMIC_STORE(&h->running, 1);
    }
  }
}

/* ===== Background loop: libusb events ===== */
#ifdef _WIN32
static DWORD WINAPI usb_loop(LPVOID p) {
  struct tc001_handle* h = (struct tc001_handle*)p;
  while (TC001_ATOMIC_LOAD(&h->running)) {
    struct timeval tv = {0, 20000}; /* 20 ms */
    libusb_handle_events_timeout_completed(h->ctx, &tv, NULL);
  }
  return 0;
}
#else
#include <pthread.h>
static void* usb_loop(void* p) {
  struct tc001_handle* h = (struct tc001_handle*)p;
  while (TC001_ATOMIC_LOAD(&h->running)) {
    struct timeval tv = {0, 20000};
    libusb_handle_events_timeout_completed(h->ctx, &tv, NULL);
  }
  return NULL;
}
#endif

/* ===== Public API ===== */
tc001_status tc001_open(tc001_handle** out, uint16_t vid, uint16_t pid,
                        char* err, size_t errcap)
{
  if (!out) return TC001_ERR_PARAM;
  *out = NULL;

  struct tc001_handle* h = (struct tc001_handle*)calloc(1, sizeof(*h));
  if (!h) { seterr(err, errcap, "alloc"); return TC001_ERR_ALLOC; }

  if (libusb_init(&h->ctx) < 0) {
    free(h); seterr(err, errcap, "libusb_init"); return TC001_ERR_USB;
  }

  if (!vid && !pid) { vid = DEF_VENDOR_ID; pid = DEF_PRODUCT_ID; }

  h->dev = libusb_open_device_with_vid_pid(h->ctx, vid, pid);
  if (!h->dev) {
    libusb_exit(h->ctx); free(h);
    seterr(err, errcap, "device not found");
    return TC001_ERR_NO_DEV;
  }

  if (libusb_claim_interface(h->dev, INTERFACE_NUMBER) < 0) {
    libusb_close(h->dev); libusb_exit(h->ctx); free(h);
    seterr(err, errcap, "claim interface failed");
    return TC001_ERR_USB;
  }

  /* Control sequence */
  int r;
  if ((r = send_standard_set_configuration(h->dev)) < 0) {
    seterr(err, errcap, "SET_CONFIGURATION failed");
    goto FAIL_USB;
  }
  if ((r = send_vendor_setup(h->dev)) < 0) {
    seterr(err, errcap, "vendor setup failed");
    goto FAIL_USB;
  }
  if ((r = send_probe(h->dev)) < 0) {
    seterr(err, errcap, "probe failed");
    goto FAIL_USB;
  }
  if ((r = send_commit(h->dev)) < 0) {
    seterr(err, errcap, "commit failed");
    goto FAIL_USB;
  }

  if (libusb_set_interface_alt_setting(h->dev, INTERFACE_NUMBER, 7) < 0) {
    seterr(err, errcap, "set alt setting failed");
    goto FAIL_USB;
  }

  /* Buffers */
  h->iso_buf   = (uint8_t*)malloc(PACKET_SIZE * NUM_PACKETS);
  h->frame_buf = (uint8_t*)malloc(FRAME_SIZE);
  if (!h->iso_buf || !h->frame_buf) {
    seterr(err, errcap, "alloc buffers");
    goto FAIL_USB;
  }
  h->frame_pos = 0;

  *out = h;
  return TC001_OK;

FAIL_USB:
  if (h->iso_buf) free(h->iso_buf);
  if (h->frame_buf) free(h->frame_buf);
  libusb_release_interface(h->dev, INTERFACE_NUMBER);
  libusb_close(h->dev);
  libusb_exit(h->ctx);
  free(h);
  return TC001_ERR_USB;
}

void tc001_close(tc001_handle* h) {
  if (!h) return;
  tc001_stop(h);
  libusb_release_interface(h->dev, INTERFACE_NUMBER);
  libusb_close(h->dev);
  libusb_exit(h->ctx);
  free(h->iso_buf);
  free(h->frame_buf);
  free(h);
}

tc001_status tc001_start(tc001_handle* h,
                         tc001_frame_cb cb, void* user,
                         char* err, size_t errcap)
{
  if (!h || !cb) return TC001_ERR_PARAM;
  if (TC001_ATOMIC_LOAD(&h->running)) return TC001_ERR_STATE;

  h->cb = cb; h->cb_user = user;

  h->xfer = libusb_alloc_transfer(NUM_PACKETS);
  if (!h->xfer) { seterr(err, errcap, "alloc transfer"); return TC001_ERR_ALLOC; }

  libusb_fill_iso_transfer(h->xfer, h->dev, ISO_ENDPOINT,
                           h->iso_buf, PACKET_SIZE * NUM_PACKETS,
                           NUM_PACKETS, iso_cb, h, TIMEOUT_MS);
  libusb_set_iso_packet_lengths(h->xfer, PACKET_SIZE);

  if (libusb_submit_transfer(h->xfer) < 0) {
    libusb_free_transfer(h->xfer); h->xfer = NULL;
    seterr(err, errcap, "submit transfer");
    return TC001_ERR_USB;
  }

  TC001_ATOMIC_STORE(&h->running, 1);

#ifdef _WIN32
  h->thread = CreateThread(NULL, 0, usb_loop, h, 0, NULL);
  if (!h->thread) {
    TC001_ATOMIC_STORE(&h->running, 0);
    seterr(err, errcap, "CreateThread failed");
    return TC001_ERR_INTERNAL;
  }
#else
  if (pthread_create(&h->thread, NULL, usb_loop, h) != 0) {
    TC001_ATOMIC_STORE(&h->running, 0);
    seterr(err, errcap, "pthread_create failed");
    return TC001_ERR_INTERNAL;
  }
#endif
  return TC001_OK;
}

void tc001_stop(tc001_handle* h) {
  if (!h) return;
  if (!TC001_ATOMIC_LOAD(&h->running)) return;

  TC001_ATOMIC_STORE(&h->running, 0);

  if (h->xfer) {
    libusb_cancel_transfer(h->xfer);
    /* Let the event loop flush the cancel; pump events briefly */
    for (int i=0; i<10; ++i) {
      struct timeval tv = {0, 10000}; /* 10 ms */
      libusb_handle_events_timeout_completed(h->ctx, &tv, NULL);
    }
    libusb_free_transfer(h->xfer);
    h->xfer = NULL;
  }

#ifdef _WIN32
  WaitForSingleObject(h->thread, INFINITE);
  CloseHandle(h->thread);
  h->thread = NULL;
#else
  pthread_join(h->thread, NULL);
#endif
}

void tc001_get_frame_dims(tc001_handle* h, int* w, int* hgt) {
  (void)h;
  if (w) *w = FRAME_WIDTH;
  if (hgt) *hgt = FRAME_HEIGHT;
}

void tc001_u16_to_u8(const uint16_t* in, int count, uint8_t* out) {
  /* simple percentile AGC */
  if (!in || !out || count <= 0) return;
  uint16_t lo = 65535, hi = 0;
  for (int i=0;i<count;i++){ if(in[i]<lo)lo=in[i]; if(in[i]>hi)hi=in[i]; }
  float span = (float)(hi - lo); if (span < 1.f) span = 1.f;
  for (int i=0;i<count;i++){
    float v = (in[i]-lo)/span;
    int u = (int)(v*255.f + 0.5f);
    if (u<0) u=0; if (u>255) u=255;
    out[i] = (uint8_t)u;
  }
}
