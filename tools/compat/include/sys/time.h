/*
 * Minimal <sys/time.h> for the MSVC build: struct timeval (from Winsock)
 * and gettimeofday() implemented with GLib's wall clock.
 */

#ifndef MSVC_COMPAT_SYS_TIME_H
#define MSVC_COMPAT_SYS_TIME_H

#include <time.h>
#include <winsock2.h> /* struct timeval */
#include <glib.h>

struct timezone;

static __inline int
gettimeofday (struct timeval *tv,
	      struct timezone *tz)
{
	gint64 now = g_get_real_time ();

	(void) tz;

	tv->tv_sec = (long) (now / G_USEC_PER_SEC);
	tv->tv_usec = (long) (now % G_USEC_PER_SEC);

	return 0;
}

#endif /* MSVC_COMPAT_SYS_TIME_H */
