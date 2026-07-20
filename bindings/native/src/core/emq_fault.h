#ifndef EMQ_FAULT_H
#define EMQ_FAULT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fault injection (compiled only when EMQ_FAULT_INJECT is defined).
 * When disabled, all macros expand to no-ops / pass-through.
 */

#if defined(EMQ_FAULT_INJECT)

typedef enum emq_fault_mode {
  EMQ_FAULT_OFF = 0,
  EMQ_FAULT_EVERY_N = 1,   /* fail every N-th hit */
  EMQ_FAULT_AFTER_N = 2,   /* fail after N hits (then every call) */
  EMQ_FAULT_PROB = 3       /* fail with probability p/10000 */
} emq_fault_mode;

void emq_fault_init(void);
void emq_fault_reset(void);
void emq_fault_configure(const char *name, emq_fault_mode mode, uint32_t n,
                         uint32_t p_tenths_pct);
int emq_fault_should_fail(const char *name);
void emq_fault_crashpoint(const char *name);
void emq_fault_set_seed(uint64_t seed);

/* Parse EMQ_FAULT=name:mode:n and EMQ_CRASH_AT=name:hit from env. */
void emq_fault_load_env(void);

#  define EMQ_FAULT_CHECK(name) emq_fault_should_fail(name)
#  define EMQ_CRASHPOINT(name) emq_fault_crashpoint(name)

#else

#  define EMQ_FAULT_CHECK(name) (0)
#  define EMQ_CRASHPOINT(name) ((void)0)

static inline void emq_fault_init(void) {}
static inline void emq_fault_reset(void) {}
static inline void emq_fault_configure(const char *n, int m, uint32_t a,
                                       uint32_t b) {
  (void)n;
  (void)m;
  (void)a;
  (void)b;
}
static inline void emq_fault_load_env(void) {}
static inline void emq_fault_set_seed(uint64_t s) { (void)s; }

#endif

#ifdef __cplusplus
}
#endif

#endif /* EMQ_FAULT_H */
