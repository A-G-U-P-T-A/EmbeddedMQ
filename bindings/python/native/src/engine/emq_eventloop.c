#include "engine/emq_eventloop.h"
#include "platform/emq_platform.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <io.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#  include <sys/event.h>
#  include <unistd.h>
#elif defined(__linux__)
#  include <sys/epoll.h>
#  include <unistd.h>
#endif

#define EMQ_EV_MAX 64

typedef struct emq_ev_reg {
  int fd;
  uint32_t events;
  emq_event_cb cb;
  void *user;
  int active;
#if defined(_WIN32)
  int associated;
#endif
} emq_ev_reg;

struct emq_eventloop {
#if defined(_WIN32)
  HANDLE iocp;
#elif defined(__APPLE__) || defined(__FreeBSD__)
  int kq;
#elif defined(__linux__)
  int epfd;
#else
  int dummy;
#endif
  emq_ev_reg regs[EMQ_EV_MAX];
  uint32_t count;
};

int emq_eventloop_create(emq_eventloop **out) {
  emq_eventloop *loop;
  if (!out) return -1;
  loop = (emq_eventloop *)calloc(1, sizeof(*loop));
  if (!loop) return -2;
#if defined(_WIN32)
  loop->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  if (!loop->iocp) {
    free(loop);
    return -6;
  }
#elif defined(__APPLE__) || defined(__FreeBSD__)
  loop->kq = kqueue();
  if (loop->kq < 0) {
    free(loop);
    return -6;
  }
#elif defined(__linux__)
  loop->epfd = epoll_create1(0);
  if (loop->epfd < 0) {
    free(loop);
    return -6;
  }
#endif
  *out = loop;
  return 0;
}

void emq_eventloop_destroy(emq_eventloop *loop) {
  if (!loop) return;
#if defined(_WIN32)
  if (loop->iocp) CloseHandle(loop->iocp);
#elif defined(__APPLE__) || defined(__FreeBSD__)
  if (loop->kq >= 0) close(loop->kq);
#elif defined(__linux__)
  if (loop->epfd >= 0) close(loop->epfd);
#endif
  free(loop);
}

int emq_eventloop_add(emq_eventloop *loop, int fd, uint32_t events, emq_event_cb cb, void *user) {
  uint32_t i;
  uint32_t free_slot = EMQ_EV_MAX;
#if defined(_WIN32)
  int was_associated;
#endif
  if (!loop || fd < 0 || !cb ||
      (events & (EMQ_EV_READ | EMQ_EV_WRITE)) == 0 ||
      (events & ~(EMQ_EV_READ | EMQ_EV_WRITE)) != 0) return -1;

  for (i = 0; i < EMQ_EV_MAX; ++i) {
    if (loop->regs[i].active && loop->regs[i].fd == fd) return -8;
#if defined(_WIN32)
    if (!loop->regs[i].active && loop->regs[i].associated &&
        loop->regs[i].fd == fd) {
      free_slot = i;
      break;
    }
    if (free_slot == EMQ_EV_MAX && !loop->regs[i].associated) free_slot = i;
#else
    if (free_slot == EMQ_EV_MAX && !loop->regs[i].active) free_slot = i;
#endif
  }
  if (free_slot == EMQ_EV_MAX) return -4;
  i = free_slot;

#if defined(_WIN32)
  was_associated = loop->regs[i].associated;
#endif
  loop->regs[i].fd = fd;
  loop->regs[i].events = events;
  loop->regs[i].cb = cb;
  loop->regs[i].user = user;

#if defined(_WIN32)
  {
    intptr_t native = _get_osfhandle(fd);
    HANDLE handle;
    if (native == (intptr_t)-1) native = (intptr_t)(uintptr_t)(unsigned int)fd;
    handle = (HANDLE)native;
    if (!CreateIoCompletionPort(handle, loop->iocp, (ULONG_PTR)i + 1u, 0)) {
      if (was_associated) {
        loop->regs[i].events = 0;
        loop->regs[i].cb = NULL;
        loop->regs[i].user = NULL;
      } else {
        memset(&loop->regs[i], 0, sizeof(loop->regs[i]));
      }
      return -6;
    }
    loop->regs[i].associated = 1;
  }
#elif defined(__linux__)
  {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    if (events & EMQ_EV_READ) ev.events |= EPOLLIN;
    if (events & EMQ_EV_WRITE) ev.events |= EPOLLOUT;
    ev.data.u32 = i;
    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
      memset(&loop->regs[i], 0, sizeof(loop->regs[i]));
      return -6;
    }
  }
#elif defined(__APPLE__) || defined(__FreeBSD__)
  {
    struct kevent changes[2];
    int nchanges = 0;
    if (events & EMQ_EV_READ) {
      EV_SET(&changes[nchanges++], (uintptr_t)fd, EVFILT_READ, EV_ADD,
             0, 0, (void *)(uintptr_t)i);
    }
    if (events & EMQ_EV_WRITE) {
      EV_SET(&changes[nchanges++], (uintptr_t)fd, EVFILT_WRITE, EV_ADD,
             0, 0, (void *)(uintptr_t)i);
    }
    if (kevent(loop->kq, changes, nchanges, NULL, 0, NULL) < 0) {
      int j;
      for (j = 0; j < nchanges; ++j) changes[j].flags = EV_DELETE;
      (void)kevent(loop->kq, changes, nchanges, NULL, 0, NULL);
      memset(&loop->regs[i], 0, sizeof(loop->regs[i]));
      return -6;
    }
  }
#endif

  loop->regs[i].active = 1;
  loop->count++;
  return 0;
}

int emq_eventloop_del(emq_eventloop *loop, int fd) {
  uint32_t i;
  if (!loop) return -1;
  for (i = 0; i < EMQ_EV_MAX; ++i) {
    if (loop->regs[i].active && loop->regs[i].fd == fd) {
#if defined(__linux__)
      (void)epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);
#elif defined(__APPLE__) || defined(__FreeBSD__)
      {
        struct kevent changes[2];
        int nchanges = 0;
        if (loop->regs[i].events & EMQ_EV_READ) {
          EV_SET(&changes[nchanges++], (uintptr_t)fd, EVFILT_READ,
                 EV_DELETE, 0, 0, NULL);
        }
        if (loop->regs[i].events & EMQ_EV_WRITE) {
          EV_SET(&changes[nchanges++], (uintptr_t)fd, EVFILT_WRITE,
                 EV_DELETE, 0, 0, NULL);
        }
        (void)kevent(loop->kq, changes, nchanges, NULL, 0, NULL);
      }
#endif
      loop->regs[i].active = 0;
      loop->regs[i].events = 0;
      loop->regs[i].cb = NULL;
      loop->regs[i].user = NULL;
      loop->count--;
      return 0;
    }
  }
  return -3;
}

int emq_eventloop_poll(emq_eventloop *loop, uint32_t timeout_ms) {
  if (!loop) return -1;
#if defined(_WIN32)
  {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    LPOVERLAPPED ov = NULL;
    BOOL ok = GetQueuedCompletionStatus(loop->iocp, &bytes, &key, &ov, timeout_ms);
    (void)bytes;
    if (ok || ov != NULL) {
      if (key > 0) {
        uint32_t idx = (uint32_t)(key - 1u);
        if (idx < EMQ_EV_MAX && loop->regs[idx].active) {
          emq_ev_reg *reg = &loop->regs[idx];
          reg->cb(reg->user, reg->fd, reg->events);
          return 1;
        }
      }
      return 0;
    }
    return GetLastError() == WAIT_TIMEOUT ? 0 : -6;
  }
#elif defined(__linux__)
  {
    struct epoll_event evs[16];
    int n = epoll_wait(loop->epfd, evs, 16, (int)timeout_ms);
    int i;
    for (i = 0; i < n; ++i) {
      uint32_t idx = evs[i].data.u32;
      if (idx < EMQ_EV_MAX && loop->regs[idx].active) {
        uint32_t e = 0;
        if (evs[i].events & EPOLLIN) e |= EMQ_EV_READ;
        if (evs[i].events & EPOLLOUT) e |= EMQ_EV_WRITE;
        if (evs[i].events & (EPOLLERR | EPOLLHUP)) {
          e |= loop->regs[idx].events;
        }
        loop->regs[idx].cb(loop->regs[idx].user, loop->regs[idx].fd, e);
      }
    }
    return n < 0 ? -6 : n;
  }
#elif defined(__APPLE__) || defined(__FreeBSD__)
  {
    struct kevent evs[16];
    struct timespec ts;
    int n;
    ts.tv_sec = (time_t)(timeout_ms / 1000u);
    ts.tv_nsec = (long)((timeout_ms % 1000u) * 1000000u);
    n = kevent(loop->kq, NULL, 0, evs, 16, &ts);
    {
      int i;
      for (i = 0; i < n; ++i) {
        uint32_t idx = (uint32_t)(uintptr_t)evs[i].udata;
        if (idx < EMQ_EV_MAX && loop->regs[idx].active) {
          uint32_t e = 0;
          if (evs[i].filter == EVFILT_READ) e |= EMQ_EV_READ;
          if (evs[i].filter == EVFILT_WRITE) e |= EMQ_EV_WRITE;
          loop->regs[idx].cb(loop->regs[idx].user, loop->regs[idx].fd, e);
        }
      }
    }
    return n < 0 ? -6 : n;
  }
#else
  emq_sleep_ms(timeout_ms);
  return 0;
#endif
}
