#ifndef EMQ_TESTSUPPORT_H
#define EMQ_TESTSUPPORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Seeded RNG (xoshiro256**) ---- */
typedef struct emq_rng {
  uint64_t s[4];
} emq_rng;

void emq_rng_seed(emq_rng *r, uint64_t seed);
uint64_t emq_rng_u64(emq_rng *r);
uint32_t emq_rng_u32(emq_rng *r);
uint64_t emq_rng_bounded(emq_rng *r, uint64_t n); /* [0, n) */
double emq_rng_uniform(emq_rng *r);               /* [0, 1) */

/* ---- CLI helpers ---- */
typedef struct emq_cli {
  uint64_t ops;
  uint64_t duration_sec;
  uint32_t threads;
  uint32_t queues;
  uint64_t seed;
  uint32_t cycles;
  uint32_t payload;
  int quick;
  const char *csv_path;
  const char *path; /* durable data dir */
} emq_cli;

void emq_cli_defaults(emq_cli *c);
int emq_cli_parse(emq_cli *c, int argc, char **argv);
uint64_t emq_cli_seed_or_time(const emq_cli *c);

/* ---- Self-verifying payloads ----
 * Layout: [u64 seq][u32 producer][u32 checksum][body...]
 * checksum = FNV-1a over (seq, producer, body).
 */
enum { EMQ_PAYLOAD_HDR = 16 };

int emq_payload_fill(uint8_t *buf, size_t len, uint64_t seq, uint32_t producer);
int emq_payload_check(const uint8_t *buf, size_t len, uint64_t *seq_out,
                      uint32_t *producer_out);

/* ---- Progress watchdog (deadlock / livelock detector) ---- */
typedef struct emq_watchdog emq_watchdog;

emq_watchdog *emq_watchdog_start(uint32_t stall_sec, const char *label);
void emq_watchdog_heartbeat(emq_watchdog *w);
void emq_watchdog_stop(emq_watchdog *w);

/* ---- Process metrics (handles + RSS) ---- */
typedef struct emq_proc_snap {
  uint64_t rss_bytes;
  uint64_t peak_rss_bytes;
  uint64_t handle_count;
  uint64_t user_time_ns;
  uint64_t kernel_time_ns;
} emq_proc_snap;

int emq_proc_sample(emq_proc_snap *out);

/* ---- Tiny assert helpers for stress/fuzz (abort on fail) ---- */
#define EMQ_REQUIRE(cond)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "REQUIRE fail %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* EMQ_TESTSUPPORT_H */
