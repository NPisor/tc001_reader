#include <pthread.h>
#include <time.h>
#include <unistd.h>

typedef pthread_t tc001_thread_t;

static int tc001_thread_create(tc001_thread_t* t, void*(*fn)(void*), void* arg) {
  return pthread_create(t, NULL, fn, arg);
}
static void tc001_thread_join(tc001_thread_t t) { pthread_join(t, NULL); }
static void tc001_sleep_ms(int ms) { usleep(ms * 1000); }
