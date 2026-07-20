#include "routing/emq_routing.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

struct emq_route_group {
  char pattern[EMQ_TOPIC_MAX];
  char name[EMQ_NAME_MAX];
  uint64_t offset;
  emq_log *inbox;
  emq_route_sub *members;
  emq_route_sub *rr_next;
  uint32_t member_count;
  void *mu_opaque;
};

static int emq_is_sep(char c) {
  return c == '.' || c == '/';
}

/* MQTT-style wildcards over '.' or '/' separators:
 *   *  = one level
 *   #  = zero or more levels (must be last token)
 */
int emq_topic_match(const char *pattern, const char *topic) {
  const char *p = pattern;
  const char *t = topic;
  if (!p || !t) return 0;

  while (*p && *t) {
    if (*p == '#') {
      return *(p + 1) == '\0';
    }
    if (*p == '*') {
      /* consume one topic level */
      while (*t && !emq_is_sep(*t)) t++;
      p++;
      if (emq_is_sep(*p) && emq_is_sep(*t)) {
        p++;
        t++;
        continue;
      }
      return *p == '\0' && *t == '\0';
    }
    if (emq_is_sep(*p) && emq_is_sep(*t)) {
      p++;
      t++;
      continue;
    }
    if (*p == *t) {
      p++;
      t++;
      continue;
    }
    return 0;
  }

  if (*p == '#') return *(p + 1) == '\0';
  if (*p == '*' && *(p + 1) == '\0' && *t == '\0') return 1;
  /* "# alone" or trailing "# after separator already consumed */
  if (*p == '/' || *p == '.') {
    if (*(p + 1) == '#' && *(p + 2) == '\0') return 1;
  }
  return *p == '\0' && *t == '\0';
}

static int emq_router_reserve_subs(emq_router *r) {
  uint32_t ncap;
  emq_route_sub **ns;
  if (r->count < r->capacity) return 0;
  ncap = r->capacity ? r->capacity * 2u : 64u;
  ns = (emq_route_sub **)realloc(r->subs, sizeof(*ns) * ncap);
  if (!ns) return -2;
  memset(ns + r->capacity, 0, sizeof(*ns) * (ncap - r->capacity));
  r->subs = ns;
  r->capacity = ncap;
  return 0;
}

static int emq_router_reserve_groups(emq_router *r) {
  uint32_t ncap;
  emq_route_group **ng;
  if (r->group_count < r->group_capacity) return 0;
  ncap = r->group_capacity ? r->group_capacity * 2u : 16u;
  ng = (emq_route_group **)realloc(r->groups, sizeof(*ng) * ncap);
  if (!ng) return -2;
  memset(ng + r->group_capacity, 0,
         sizeof(*ng) * (ncap - r->group_capacity));
  r->groups = ng;
  r->group_capacity = ncap;
  return 0;
}

static emq_route_group *emq_router_find_group(emq_router *r,
                                               const char *pattern,
                                               const char *name) {
  uint32_t i;
  for (i = 0; i < r->group_capacity; ++i) {
    emq_route_group *g = r->groups[i];
    if (g && strcmp(g->pattern, pattern) == 0 &&
        strcmp(g->name, name) == 0) {
      return g;
    }
  }
  return NULL;
}

static void emq_route_group_destroy(emq_route_group *g) {
  if (!g) return;
  emq_log_close(g->inbox);
  emq_mutex_destroy((emq_mutex *)g->mu_opaque);
  free(g);
}

static int emq_route_group_create(const char *pattern, const char *name,
                                  emq_route_group **out) {
  emq_route_group *g;
  if (!out) return -1;
  g = (emq_route_group *)calloc(1, sizeof(*g));
  if (!g) return -2;
  strcpy(g->pattern, pattern);
  strcpy(g->name, name);
  g->mu_opaque = emq_mutex_create();
  if (!g->mu_opaque ||
      emq_log_open(&g->inbox, EMQ_STORAGE_FAST, NULL, 1024,
                   EMQ_FSYNC_NONE) != 0) {
    emq_route_group_destroy(g);
    return -2;
  }
  *out = g;
  return 0;
}

static void emq_route_group_sync_offsets(emq_route_group *g) {
  emq_route_sub *member = g->members;
  while (member) {
    member->offset = g->offset;
    member = member->group_next;
  }
}

static void emq_route_group_add_member(emq_route_group *g,
                                       emq_route_sub *sub) {
  emq_route_sub **tail = &g->members;
  while (*tail) tail = &(*tail)->group_next;
  *tail = sub;
  sub->group_next = NULL;
  sub->offset = g->offset;
  g->member_count++;
  if (!g->rr_next) g->rr_next = sub;
}

static void emq_route_group_remove_member(emq_route_group *g,
                                          emq_route_sub *sub) {
  emq_route_sub **link = &g->members;
  emq_route_sub *successor;
  while (*link && *link != sub) link = &(*link)->group_next;
  if (!*link) return;
  successor = sub->group_next;
  *link = sub->group_next;
  if (g->member_count) g->member_count--;
  if (g->rr_next == sub) {
    g->rr_next = successor ? successor : g->members;
  }
  sub->group_next = NULL;
  emq_route_group_sync_offsets(g);
}

static int emq_route_append_ref(emq_log *inbox, uint32_t owner_id,
                                uint64_t payload_offset,
                                uint32_t route_flags,
                                uint64_t deliver_at_ns) {
  uint64_t ignored_offset;
  return emq_log_append(inbox, owner_id, payload_offset, route_flags,
                        deliver_at_ns, NULL, 0, &ignored_offset);
}

static int emq_router_route_ref_locked(emq_router *r, const char *topic,
                                       uint64_t payload_offset,
                                       uint32_t route_flags,
                                       uint64_t deliver_at_ns) {
  uint32_t i;
  int first_error = 0;
  for (i = 0; i < r->capacity; ++i) {
    emq_route_sub *s = r->subs[i];
    int rc;
    if (!s || !s->active || s->shared_group) continue;
    if (!emq_topic_match(s->pattern, topic)) continue;
    rc = emq_route_append_ref(s->inbox, s->id, payload_offset,
                              route_flags, deliver_at_ns);
    if (rc != 0 && first_error == 0) first_error = rc;
  }
  for (i = 0; i < r->group_capacity; ++i) {
    emq_route_group *g = r->groups[i];
    int rc;
    if (!g || g->member_count == 0) continue;
    if (!emq_topic_match(g->pattern, topic)) continue;
    rc = emq_route_append_ref(g->inbox, 0, payload_offset,
                              route_flags, deliver_at_ns);
    if (rc != 0 && first_error == 0) first_error = rc;
  }
  return first_error;
}

int emq_router_init(emq_router *r) {
  if (!r) return -1;
  memset(r, 0, sizeof(*r));
  r->capacity = 64;
  r->group_capacity = 16;
  r->subs = (emq_route_sub **)calloc(r->capacity, sizeof(*r->subs));
  r->groups = (emq_route_group **)calloc(r->group_capacity,
                                         sizeof(*r->groups));
  r->mu_opaque = emq_mutex_create();
  r->next_id = 1;
  if (!r->subs || !r->groups || !r->mu_opaque) {
    emq_router_destroy(r);
    return -2;
  }
  if (emq_log_open(&r->payload_log, EMQ_STORAGE_FAST, NULL, 4096,
                   EMQ_FSYNC_NONE) != 0) {
    emq_router_destroy(r);
    return -2;
  }
  return 0;
}

void emq_router_destroy(emq_router *r) {
  uint32_t i;
  if (!r) return;
  if (r->subs) {
    for (i = 0; i < r->capacity; ++i) {
      emq_route_sub *s = r->subs[i];
      if (!s) continue;
      s->active = 0;
      if (!s->shared_group) {
        emq_log_close(s->inbox);
        emq_mutex_destroy((emq_mutex *)s->consume_mu_opaque);
      }
      free(s);
    }
    free(r->subs);
  }
  if (r->groups) {
    for (i = 0; i < r->group_capacity; ++i) {
      emq_route_group_destroy(r->groups[i]);
    }
    free(r->groups);
  }
  emq_log_close(r->payload_log);
  emq_mutex_destroy((emq_mutex *)r->mu_opaque);
  memset(r, 0, sizeof(*r));
}

int emq_router_subscribe(emq_router *r, const char *pattern, const char *group,
                         emq_route_sub **out) {
  emq_route_sub *s;
  emq_route_group *g = NULL;
  uint32_t i;
  int group_created = 0;
  int rc;
  if (!r || !pattern || !out || !pattern[0]) return -1;
  if (strlen(pattern) >= EMQ_TOPIC_MAX ||
      (group && strlen(group) >= EMQ_NAME_MAX)) {
    return -1;
  }

  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  rc = emq_router_reserve_subs(r);
  if (rc != 0) {
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return rc;
  }

  if (group && group[0]) {
    g = emq_router_find_group(r, pattern, group);
    if (!g) {
      rc = emq_router_reserve_groups(r);
      if (rc == 0) rc = emq_route_group_create(pattern, group, &g);
      if (rc != 0) {
        emq_mutex_unlock((emq_mutex *)r->mu_opaque);
        return rc;
      }
      group_created = 1;
    }
  }

  s = (emq_route_sub *)calloc(1, sizeof(*s));
  if (!s) {
    if (group_created) emq_route_group_destroy(g);
    emq_mutex_unlock((emq_mutex *)r->mu_opaque);
    return -2;
  }
  strcpy(s->pattern, pattern);
  if (group) strcpy(s->group, group);
  s->id = r->next_id++;
  s->active = 1;
  s->router = r;
  s->shared_group = g;

  if (g) {
    s->inbox = g->inbox;
    emq_mutex_lock((emq_mutex *)g->mu_opaque);
    emq_route_group_add_member(g, s);
    emq_mutex_unlock((emq_mutex *)g->mu_opaque);
    if (group_created) {
      for (i = 0; i < r->group_capacity; ++i) {
        if (!r->groups[i]) {
          r->groups[i] = g;
          r->group_count++;
          break;
        }
      }
    }
  } else {
    s->consume_mu_opaque = emq_mutex_create();
    if (!s->consume_mu_opaque ||
        emq_log_open(&s->inbox, EMQ_STORAGE_FAST, NULL, 1024,
                     EMQ_FSYNC_NONE) != 0) {
      emq_log_close(s->inbox);
      emq_mutex_destroy((emq_mutex *)s->consume_mu_opaque);
      free(s);
      emq_mutex_unlock((emq_mutex *)r->mu_opaque);
      return -2;
    }
  }

  for (i = 0; i < r->capacity; ++i) {
    if (!r->subs[i]) {
      r->subs[i] = s;
      break;
    }
  }
  r->count++;
  *out = s;
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return 0;
}

int emq_router_unsubscribe(emq_router *r, uint32_t sub_id) {
  uint32_t i;
  if (!r) return -1;
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  for (i = 0; i < r->capacity; ++i) {
    emq_route_sub *s = r->subs[i];
    if (s && s->id == sub_id) {
      emq_route_group *g = s->shared_group;
      r->subs[i] = NULL;
      r->count--;
      if (g) {
        uint32_t j;
        emq_mutex_lock((emq_mutex *)g->mu_opaque);
        s->active = 0;
        emq_route_group_remove_member(g, s);
        emq_mutex_unlock((emq_mutex *)g->mu_opaque);
        if (g->member_count == 0) {
          for (j = 0; j < r->group_capacity; ++j) {
            if (r->groups[j] == g) {
              r->groups[j] = NULL;
              r->group_count--;
              break;
            }
          }
          emq_route_group_destroy(g);
        }
      } else {
        emq_mutex_lock((emq_mutex *)s->consume_mu_opaque);
        s->active = 0;
        emq_mutex_unlock((emq_mutex *)s->consume_mu_opaque);
        emq_log_close(s->inbox);
        emq_mutex_destroy((emq_mutex *)s->consume_mu_opaque);
      }
      free(s);
      emq_mutex_unlock((emq_mutex *)r->mu_opaque);
      return 0;
    }
  }
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return -3;
}

int emq_router_publish_ex(emq_router *r, const char *topic,
                          const void *data, size_t size,
                          const emq_message *meta, uint64_t *out_offset) {
  uint64_t payload_off;
  uint64_t deliver_at_ns = meta ? meta->deliver_at_ns : 0;
  uint32_t priority = meta ? meta->priority : 0;
  int rc;
  if (!r || !topic || !topic[0] || (size && !data) ||
      size > (size_t)UINT32_MAX) {
    return -1;
  }

  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  payload_off = emq_log_next_offset(r->payload_log);
  rc = emq_log_append(r->payload_log, 0, payload_off, priority,
                      deliver_at_ns, data, (uint32_t)size, &payload_off);
  if (rc == 0) {
    rc = emq_router_route_ref_locked(r, topic, payload_off, 0,
                                     deliver_at_ns);
  }
  if (out_offset) *out_offset = payload_off;
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return rc;
}

int emq_router_publish(emq_router *r, const char *topic,
                       const void *data, size_t size, uint64_t *out_offset) {
  return emq_router_publish_ex(r, topic, data, size, NULL, out_offset);
}

int emq_router_publish_batch(emq_router *r, const char *topic,
                             const emq_batch_item *items, size_t count,
                             size_t *published) {
  size_t i;
  if (published) *published = 0;
  if (!r || !topic || (!items && count != 0)) return -1;
  for (i = 0; i < count; ++i) {
    uint64_t off = 0;
    int rc = emq_router_publish_ex(r, topic, items[i].data, items[i].size,
                                   &items[i].meta, &off);
    if (rc != 0) {
      if (published) *published = i;
      return rc;
    }
  }
  if (published) *published = count;
  return 0;
}

static int emq_router_next_at(emq_route_sub *sub, uint64_t *cursor,
                              emq_message *out) {
  emq_log_entry ref;
  emq_log_entry payload;
  uint64_t next = *cursor;
  uint64_t end = emq_log_next_offset(sub->inbox);
  while (next < end) {
    int rc = emq_log_read(sub->inbox, next, &ref);
    if (rc != 0) {
      next++;
      continue;
    }
    if (ref.deliver_at_ns != 0 && ref.deliver_at_ns > emq_now_ns()) {
      return -5;
    }
    rc = emq_log_read_copy(sub->router->payload_log, ref.msg_id, &payload);
    if (rc != 0) return rc;
    memset(out, 0, sizeof(*out));
    out->id = ref.msg_id;
    out->offset = ref.offset;
    out->data = payload.payload;
    out->size = payload.payload_len;
    out->priority = payload.priority;
    out->deliver_at_ns = ref.deliver_at_ns;
    out->flags = ref.priority;
    *cursor = ref.offset + 1;
    return 0;
  }
  return -5;
}

int emq_router_next(emq_route_sub *sub, emq_message *out) {
  int rc;
  uint64_t cursor;
  if (!sub || !out || !sub->inbox || !sub->router) return -1;
  if (sub->shared_group) {
    emq_route_group *g = sub->shared_group;
    emq_mutex_lock((emq_mutex *)g->mu_opaque);
    if (!sub->active || g->rr_next != sub) {
      emq_mutex_unlock((emq_mutex *)g->mu_opaque);
      return -5;
    }
    cursor = g->offset;
    rc = emq_router_next_at(sub, &cursor, out);
    if (rc == 0) {
      g->offset = cursor;
      emq_route_group_sync_offsets(g);
      g->rr_next = sub->group_next ? sub->group_next : g->members;
    }
    emq_mutex_unlock((emq_mutex *)g->mu_opaque);
    return rc;
  }

  emq_mutex_lock((emq_mutex *)sub->consume_mu_opaque);
  if (!sub->active) {
    emq_mutex_unlock((emq_mutex *)sub->consume_mu_opaque);
    return -9;
  }
  cursor = sub->offset;
  rc = emq_router_next_at(sub, &cursor, out);
  if (rc == 0) sub->offset = cursor;
  emq_mutex_unlock((emq_mutex *)sub->consume_mu_opaque);
  return rc;
}

int emq_router_seek(emq_route_sub *sub, uint64_t offset) {
  if (!sub || !sub->inbox) return -1;
  if (sub->shared_group) {
    emq_route_group *g = sub->shared_group;
    emq_mutex_lock((emq_mutex *)g->mu_opaque);
    if (!sub->active) {
      emq_mutex_unlock((emq_mutex *)g->mu_opaque);
      return -9;
    }
    g->offset = offset;
    g->rr_next = sub;
    emq_route_group_sync_offsets(g);
    emq_mutex_unlock((emq_mutex *)g->mu_opaque);
    return 0;
  }
  emq_mutex_lock((emq_mutex *)sub->consume_mu_opaque);
  if (!sub->active) {
    emq_mutex_unlock((emq_mutex *)sub->consume_mu_opaque);
    return -9;
  }
  sub->offset = offset;
  emq_mutex_unlock((emq_mutex *)sub->consume_mu_opaque);
  return 0;
}

int emq_router_replay(emq_route_sub *sub) {
  return emq_router_seek(sub, 0);
}

int emq_router_retry(emq_router *r, const char *retry_topic,
                     uint64_t payload_offset, uint32_t attempt,
                     uint64_t deliver_at_ns) {
  emq_log_entry payload;
  uint32_t route_flags;
  int rc;
  if (!r || !retry_topic || !retry_topic[0] ||
      attempt > EMQ_ROUTE_ATTEMPT_MASK) {
    return -1;
  }
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  rc = emq_log_read(r->payload_log, payload_offset, &payload);
  if (rc == 0) {
    route_flags = EMQ_ROUTE_FLAG_RETRY | attempt;
    rc = emq_router_route_ref_locked(r, retry_topic, payload_offset,
                                     route_flags, deliver_at_ns);
  }
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return rc;
}

int emq_router_dead_letter(emq_router *r, const char *dead_letter_topic,
                           uint64_t payload_offset, uint32_t attempts) {
  emq_log_entry payload;
  uint32_t route_flags;
  int rc;
  if (!r || !dead_letter_topic || !dead_letter_topic[0] ||
      attempts > EMQ_ROUTE_ATTEMPT_MASK) {
    return -1;
  }
  emq_mutex_lock((emq_mutex *)r->mu_opaque);
  rc = emq_log_read(r->payload_log, payload_offset, &payload);
  if (rc == 0) {
    route_flags = EMQ_ROUTE_FLAG_DEAD_LETTER | attempts;
    rc = emq_router_route_ref_locked(r, dead_letter_topic, payload_offset,
                                     route_flags, 0);
  }
  emq_mutex_unlock((emq_mutex *)r->mu_opaque);
  return rc;
}
