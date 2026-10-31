/*
 * <utime.h> for the MSVC build: the CRT has it as <sys/utime.h>, with
 * struct utimbuf and utime() under their POSIX names.
 */

#ifndef MSVC_COMPAT_UTIME_H
#define MSVC_COMPAT_UTIME_H

#include <sys/utime.h>

#endif /* MSVC_COMPAT_UTIME_H */
