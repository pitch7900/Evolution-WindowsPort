/*
 * Minimal <dirent.h> for the MSVC build: opendir/readdir/closedir on top of
 * FindFirstFileW/FindNextFileW. Paths are UTF-8 (as everywhere in GLib) and
 * entry names are returned in UTF-8.
 */

#ifndef MSVC_COMPAT_DIRENT_H
#define MSVC_COMPAT_DIRENT_H

#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

struct dirent {
	unsigned char d_type;
	char d_name[MAX_PATH * 4]; /* UTF-8, up to 4 bytes per UTF-16 unit */
};

typedef struct {
	HANDLE handle;
	WIN32_FIND_DATAW data;
	int first;
	wchar_t *pattern; /* kept for rewinddir() */
	struct dirent entry;
} DIR;

static __inline DIR *
opendir (const char *name)
{
	DIR *dir;
	int len;
	wchar_t *pattern;

	if (!name || !*name) {
		errno = ENOENT;
		return NULL;
	}

	/* "<name>\*" as UTF-16 */
	len = MultiByteToWideChar (CP_UTF8, 0, name, -1, NULL, 0);
	if (len <= 0) {
		errno = EINVAL;
		return NULL;
	}

	pattern = (wchar_t *) malloc ((len + 2) * sizeof (wchar_t));
	if (!pattern) {
		errno = ENOMEM;
		return NULL;
	}

	MultiByteToWideChar (CP_UTF8, 0, name, -1, pattern, len);
	if (len > 1 && pattern[len - 2] != L'\\' && pattern[len - 2] != L'/')
		wcscat_s (pattern, len + 2, L"\\");
	wcscat_s (pattern, len + 2, L"*");

	dir = (DIR *) calloc (1, sizeof (DIR));
	if (!dir) {
		free (pattern);
		errno = ENOMEM;
		return NULL;
	}

	dir->handle = FindFirstFileW (pattern, &dir->data);
	dir->pattern = pattern;

	if (dir->handle == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError ();

		free (pattern);
		free (dir);
		errno = (err == ERROR_ACCESS_DENIED) ? EACCES : ENOENT;
		return NULL;
	}

	dir->first = 1;

	return dir;
}

static __inline struct dirent *
readdir (DIR *dir)
{
	if (!dir || dir->handle == INVALID_HANDLE_VALUE)
		return NULL;

	if (dir->first)
		dir->first = 0;
	else if (!FindNextFileW (dir->handle, &dir->data))
		return NULL;

	if (!WideCharToMultiByte (CP_UTF8, 0, dir->data.cFileName, -1,
		dir->entry.d_name, sizeof (dir->entry.d_name), NULL, NULL))
		dir->entry.d_name[0] = '\0';

	dir->entry.d_type = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;

	return &dir->entry;
}

static __inline int
closedir (DIR *dir)
{
	if (!dir) {
		errno = EBADF;
		return -1;
	}

	if (dir->handle != INVALID_HANDLE_VALUE)
		FindClose (dir->handle);
	free (dir->pattern);
	free (dir);

	return 0;
}

static __inline void
rewinddir (DIR *dir)
{
	if (!dir)
		return;

	if (dir->handle != INVALID_HANDLE_VALUE)
		FindClose (dir->handle);

	dir->handle = FindFirstFileW (dir->pattern, &dir->data);
	dir->first = 1;
}

#endif /* MSVC_COMPAT_DIRENT_H */
