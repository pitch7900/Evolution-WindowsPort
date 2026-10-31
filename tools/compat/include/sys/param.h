/*
 * Minimal <sys/param.h> for the MSVC build.
 */

#ifndef MSVC_COMPAT_SYS_PARAM_H
#define MSVC_COMPAT_SYS_PARAM_H

#include <stdlib.h>

#ifndef MAXPATHLEN
#define MAXPATHLEN _MAX_PATH
#endif

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

#endif /* MSVC_COMPAT_SYS_PARAM_H */
