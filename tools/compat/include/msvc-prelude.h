/*
 * Forced include (/FI) for every C and C++ file of the MSVC build.
 *
 * Keep this minimal: it only supplies POSIX types/macros that MSVC lacks and
 * that the sources use without including a specific header, and prevents
 * <windows.h> from pulling the legacy <winsock.h>, which conflicts with
 * <winsock2.h> (C2011 "struct redefinition" errors).
 */

#ifndef MSVC_COMPAT_PRELUDE_H
#define MSVC_COMPAT_PRELUDE_H

#ifdef _MSC_VER

/* <windows.h> must not include the legacy <winsock.h>; code needing sockets
 * includes <winsock2.h> itself */
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

/* min()/max() macros from <windows.h> break C++ (ICU, std::) */
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <sys/types.h>
#include <BaseTsd.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#endif

#ifndef _MODE_T_DEFINED
#define _MODE_T_DEFINED
typedef unsigned short mode_t;
#endif

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int pid_t;
#endif

/* Permission bits: only the owner bits mean something on Windows */
#ifndef S_IRUSR
#define S_IRUSR 0x0100 /* _S_IREAD */
#define S_IWUSR 0x0080 /* _S_IWRITE */
#define S_IXUSR 0x0040 /* _S_IEXEC */
#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_IRGRP (S_IRUSR >> 3)
#define S_IWGRP (S_IWUSR >> 3)
#define S_IXGRP (S_IXUSR >> 3)
#define S_IRWXG (S_IRWXU >> 3)
#define S_IROTH (S_IRUSR >> 6)
#define S_IWOTH (S_IWUSR >> 6)
#define S_IXOTH (S_IXUSR >> 6)
#define S_IRWXO (S_IRWXU >> 6)
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & 0xF000) == 0x4000) /* _S_IFDIR */
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & 0xF000) == 0x8000) /* _S_IFREG */
#endif

#ifndef O_ACCMODE
#define O_ACCMODE 0x0003 /* _O_RDONLY | _O_WRONLY | _O_RDWR */
#endif

/* Reentrant time functions: the CRT has them with swapped arguments */
#include <time.h>

static __inline struct tm *
localtime_r (const time_t *timep,
	     struct tm *result)
{
	return localtime_s (result, timep) == 0 ? result : NULL;
}

static __inline struct tm *
gmtime_r (const time_t *timep,
	  struct tm *result)
{
	return gmtime_s (result, timep) == 0 ? result : NULL;
}

#ifndef strcasecmp
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

#endif /* _MSC_VER */

#endif /* MSVC_COMPAT_PRELUDE_H */
