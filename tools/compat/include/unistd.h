/*
 * Minimal <unistd.h> for the MSVC build of Evolution and evolution-data-server.
 *
 * MSVC provides the POSIX file functions (close, read, write, lseek, unlink,
 * access, getpid, ...) under their historic names in <io.h>, <process.h> and
 * <direct.h>. This header only gathers them and adds the few missing types
 * and constants. Functions without a Windows equivalent (fork, pipe
 * semantics, fcntl, ...) are intentionally NOT provided: their callers must
 * be ported explicitly.
 */

#ifndef MSVC_COMPAT_UNISTD_H
#define MSVC_COMPAT_UNISTD_H

#ifndef _MSC_VER
#error "This header is only meant for the MSVC build"
#endif

#include "msvc-prelude.h" /* ssize_t, mode_t, pid_t */
#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdlib.h>

#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

/* access() modes; X_OK does not exist on Windows, test for existence */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef X_OK
#define X_OK F_OK
#endif

static __inline int
ftruncate (int fd,
	   __int64 length)
{
	return _chsize_s (fd, length) == 0 ? 0 : -1;
}

#endif /* MSVC_COMPAT_UNISTD_H */
