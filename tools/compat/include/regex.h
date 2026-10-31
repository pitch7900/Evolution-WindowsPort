/*
 * <regex.h> for the MSVC build: POSIX regular expressions provided by
 * PCRE2's POSIX wrapper (pcre2-posix, built by gvsbuild). pcre2posix.h maps
 * regcomp/regexec/regerror/regfree to the pcre2_* functions; link with
 * pcre2-posix (see msvc-compat.cmake).
 *
 * Note: PCRE2 implements Perl syntax; REG_EXTENDED is accepted, basic (BRE)
 * syntax specifics such as \( \) groups are not.
 */

#ifndef MSVC_COMPAT_REGEX_H
#define MSVC_COMPAT_REGEX_H

#include <pcre2posix.h>

#endif /* MSVC_COMPAT_REGEX_H */
