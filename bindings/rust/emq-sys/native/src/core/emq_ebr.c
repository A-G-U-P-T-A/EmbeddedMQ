#include "core/emq_ebr.h"
#include "core/emq_atomic.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#define EMQ_EBR_EPOCHS 3u
#define EMQ_EBR_INITIAL_THREADS 64u

typedef struct emq_ebr_node {
  void *ptr;
  emq_ebr_free_fn free_fn;
  void *arg;
  struct emq_ebr_node *next;
} emq_ebr_node;

typedef struct emq_ebr_thread {
  emq_atomic_u64 epoch;
  int active;
} emq_ebr_thread;

struct emq_ebr_domain {
  emq_atomic_u64 global_epoch;
  emq_ebr_thread *threads;
  uint32_t thread_capacity;
  emq_ebr_node *retire[EMQ_EBR_EPOCHS];
  emq_mutex *reg_mu;
};

static uint64_t emq_ebr_current_epoch(const emq_ebr_domain *domain) {
  return emq_atomic_load_u64(&domain->global_epoch) % EMQ_EBR_EPOCHS;
}

static int emq_ebr_grow_threads(emq_ebr_domain *domain) {
  uint32_t new_cap = domain->thread_capacity ? domain->thread_capacity * 2u
                                             : EMQ_EBR_INITIAL_THREADS;
  emq_ebr_thread *threads =
      (emq_ebr_thread *)realloc(domain->threads, new_cap * sizeof(*threads));
  if (!threads) return -1;
  memset(threads + domain->thread_capacity, 0,
         (new_cap - domain->thread_capacity) * sizeof(*threads));
  domain->threads = threads;
  domain->thread_capacity = new_cap;
  return 0;
}

int emq_ebr_domain_create(emq_ebr_domain **out) {
  emq_ebr_domain *domain;
  if (!out) return -1;
  domain = (emq_ebr_domain *)calloc(1, sizeof(*domain));
  if (!domain) return -2;
  domain->reg_mu = emq_mutex_create();
  if (!domain->reg_mu) {
    free(domain);
    return -2;
  }
  emq_atomic_init_u64(&domain->global_epoch, 0);
  if (emq_ebr_grow_threads(domain) != 0) {
    emq_ebr_domain_destroy(domain);
    return -2;
  }
  *out = domain;
  return 0;
}

void emq_ebr_domain_destroy(emq_ebr_domain *domain) {
  uint32_t e;
  if (!domain) return;
  for (e = 0; e < EMQ_EBR_EPOCHS; ++e) {
    emq_ebr_node *node = domain->retire[e];
    while (node) {
      emq_ebr_node *next = node->next;
      if (node->free_fn) node->free_fn(node->ptr, node->arg);
      free(node);
      node = next;
    }
  }
  free(domain->threads);
  emq_mutex_destroy(domain->reg_mu);
  free(domain);
}

int emq_ebr_register(emq_ebr_domain *domain, uint32_t *slot_out) {
  uint32_t i;
  if (!domain || !slot_out) return -1;
  emq_mutex_lock(domain->reg_mu);
  for (i = 0; i < domain->thread_capacity; ++i) {
    if (!domain->threads[i].active) {
      domain->threads[i].active = 1;
      emq_atomic_init_u64(&domain->threads[i].epoch,
                          emq_ebr_current_epoch(domain));
      *slot_out = i;
      emq_mutex_unlock(domain->reg_mu);
      return 0;
    }
  }
  if (emq_ebr_grow_threads(domain) != 0) {
    emq_mutex_unlock(domain->reg_mu);
    return -2;
  }
  for (i = 0; i < domain->thread_capacity; ++i) {
    if (!domain->threads[i].active) {
      domain->threads[i].active = 1;
      emq_atomic_init_u64(&domain->threads[i].epoch,
                          emq_ebr_current_epoch(domain));
      *slot_out = i;
      emq_mutex_unlock(domain->reg_mu);
      return 0;
    }
  }
  emq_mutex_unlock(domain->reg_mu);
  return -2;
}

void emq_ebr_unregister(emq_ebr_domain *domain, uint32_t slot) {
  if (!domain || slot >= domain->thread_capacity) return;
  emq_mutex_lock(domain->reg_mu);
  domain->threads[slot].active = 0;
  emq_atomic_store_u64(&domain->threads[slot].epoch, UINT64_MAX);
  emq_mutex_unlock(domain->reg_mu);
}

void emq_ebr_pin(emq_ebr_domain *domain, uint32_t slot) {
  if (!domain || slot >= domain->thread_capacity) return;
  emq_atomic_store_u64(&domain->threads[slot].epoch,
                       emq_atomic_load_u64(&domain->global_epoch));
}

void emq_ebr_unpin(emq_ebr_domain *domain, uint32_t slot) {
  if (!domain || slot >= domain->thread_capacity) return;
  emq_atomic_store_u64(&domain->threads[slot].epoch, UINT64_MAX);
}

void emq_ebr_retire(emq_ebr_domain *domain, void *ptr, emq_ebr_free_fn free_fn,
                    void *arg) {
  emq_ebr_node *node;
  uint64_t epoch;
  if (!domain || !ptr) return;
  node = (emq_ebr_node *)malloc(sizeof(*node));
  if (!node) {
    if (free_fn) free_fn(ptr, arg);
    return;
  }
  node->ptr = ptr;
  node->free_fn = free_fn;
  node->arg = arg;
  epoch = emq_ebr_current_epoch(domain);
  node->next = domain->retire[epoch];
  domain->retire[epoch] = node;
}

static int emq_ebr_can_free_epoch(emq_ebr_domain *domain, uint64_t epoch_slot) {
  uint32_t i;
  for (i = 0; i < domain->thread_capacity; ++i) {
    uint64_t pinned;
    if (!domain->threads[i].active) continue;
    pinned = emq_atomic_load_u64(&domain->threads[i].epoch);
    if (pinned == UINT64_MAX) continue;
    if (pinned % EMQ_EBR_EPOCHS == epoch_slot) return 0;
  }
  return 1;
}

uint32_t emq_ebr_try_reclaim(emq_ebr_domain *domain) {
  uint64_t current;
  uint64_t reclaim_epoch;
  emq_ebr_node *node;
  uint32_t freed = 0;
  if (!domain) return 0;

  current = emq_atomic_load_u64(&domain->global_epoch);
  reclaim_epoch = current % EMQ_EBR_EPOCHS;
  if (!emq_ebr_can_free_epoch(domain, reclaim_epoch)) return 0;

  node = domain->retire[reclaim_epoch];
  domain->retire[reclaim_epoch] = NULL;
  while (node) {
    emq_ebr_node *next = node->next;
    if (node->free_fn) node->free_fn(node->ptr, node->arg);
    free(node);
    freed++;
    node = next;
  }

  emq_atomic_store_u64(&domain->global_epoch, current + 1u);
  return freed;
}
