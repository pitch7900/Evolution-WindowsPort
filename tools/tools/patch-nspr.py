"""Idempotent fixes applied to the NSPR sources before building with MSVC.

Usage: python patch-nspr.py <nspr-source-dir>

configure: the MSVC version is parsed from the cl.exe banner with a sed
expression expecting a plain space before the version number. Localized
compilers (e.g. French) print a non-breaking space there, so the version is
not found ("Could not determine MSC version"). Accept any non-digit separator.
"""

import pathlib
import sys

OLD = r"""_MSVC_VER_FILTER='s|.* \([0-9]\+\.[0-9]\+\.[0-9]\+\(\.[0-9]\+\)\?\).*|\1|p'"""
NEW = r"""_MSVC_VER_FILTER='s|.*[^0-9.]\([0-9]\+\.[0-9]\+\.[0-9]\+\(\.[0-9]\+\)\?\).*|\1|p'"""


def main():
    configure = pathlib.Path(sys.argv[1]) / "configure"
    text = configure.read_bytes().decode("latin-1")

    if NEW in text:
        return 0
    if OLD not in text:
        print(f"patch-nspr: version filter not found in {configure}", file=sys.stderr)
        return 1

    configure.write_bytes(text.replace(OLD, NEW).encode("latin-1"))
    print(f"patch-nspr: patched {configure}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
