"""Adds G_MODULE_EXPORT to forward prototypes of functions defined with it.

Usage: python fix-module-export-prototypes.py <source-dir> [...]

Module sources declare their entry points first ("void e_module_load (...);")
and define them later with G_MODULE_EXPORT. GCC accepts the mismatch, MSVC
does not (C2375 "redefinition; different linkage", since G_MODULE_EXPORT is
__declspec(dllexport) there). The prototypes get the same G_MODULE_EXPORT,
which changes nothing for GCC/Clang. Idempotent; keeps line endings.
"""

import pathlib
import re
import sys

# "G_MODULE_EXPORT <type>\n<name> (" or on one line
DEFINITION = re.compile(r"G_MODULE_EXPORT\s+[^;{(]*?\b(\w+)\s*\(", re.S)


def fix_file(path):
    text = path.read_bytes().decode("utf-8")
    names = set(DEFINITION.findall(text))
    if not names:
        return False

    changed = False
    for name in names:
        # a prototype line for <name> ending with ';', without G_MODULE_EXPORT;
        # no '=' before the name: "x = name (...);" is a call, not a prototype
        proto = re.compile(r"^(?!G_MODULE_EXPORT)([ \t]*)([A-Za-z_][^;{}()=\n]*?\b%s\s*\([^;{}]*\)\s*;)" % re.escape(name), re.M)
        text, count = proto.subn(r"\1G_MODULE_EXPORT \2", text)
        changed = changed or count > 0

    if changed:
        path.write_bytes(text.encode("utf-8"))
    return changed


def main():
    total = 0
    for root in sys.argv[1:]:
        for path in sorted(pathlib.Path(root).rglob("*.c")):
            if fix_file(path):
                print(f"fixed {path}")
                total += 1
    print(f"{total} file(s) changed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
