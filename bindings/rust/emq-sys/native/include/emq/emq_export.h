#ifndef EMQ_EXPORT_H
#define EMQ_EXPORT_H

/*
 * Shared-library export control.
 * - Building the shared lib: define EMQ_BUILD_SHARED + EMQ_EXPORTS
 * - Consuming the shared lib: define EMQ_BUILD_SHARED
 * - Static linking (default): EMQ_API is empty
 */

#if defined(EMQ_BUILD_SHARED)
#  if defined(_WIN32) || defined(__CYGWIN__)
#    if defined(EMQ_EXPORTS)
#      define EMQ_API __declspec(dllexport)
#    else
#      define EMQ_API __declspec(dllimport)
#    endif
#  else
#    if defined(EMQ_EXPORTS)
#      define EMQ_API __attribute__((visibility("default")))
#    else
#      define EMQ_API
#    endif
#  endif
#else
#  define EMQ_API
#endif

#endif /* EMQ_EXPORT_H */
