#!/usr/bin/env bash
# Runs NSS' build.sh (which also builds NSPR) from MSYS2 bash with the MSVC
# environment inherited from build-nss.bat. Usage: build-nss.sh <deps-dir>
set -e

here=$(cd "$(dirname "$0")" && pwd)
deps=$(cygpath -u "$1")

# configure scripts parse cl.exe banners; avoid UTF-8 decoding of CP850 bytes
export LC_ALL=C
# NSPR needs a mingw triplet, MSYS2 reports cygwin (see nspr-config.site)
export CONFIG_SITE="$here/nspr-config.site"
# nsinstall replacement normally provided by MozillaBuild; MSVC's directory
# goes first so that its link.exe wins over coreutils' /usr/bin/link
msvc_bin=$(dirname "$(command -v cl)")
export PATH="$here/tools:$msvc_bin:$PATH"
# certdata.c generation spawns perl from Python, so it needs the native perl
# set by env.bat, not an MSYS2 one
[ -n "$PERL" ] || { echo "PERL is not set (see env.bat)" >&2; exit 1; }

python -I "$(cygpath -w "$here/tools/patch-nspr.py")" "$(cygpath -w "$deps/nspr")"

cd "$deps/nss"
./build.sh -o -v --disable-tests --target=x64
