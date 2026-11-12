<!-- cspell:disable -->
# Native Windows 11 (MSVC) port of Evolution

Working log of the `windows-native` fork (`Evolution-WindowsPort`). Goal: build and
run Evolution **natively** on Windows 11 with the Microsoft
Visual C++ compiler (no MinGW, no WSL, no Cygwin) and provide a
native **WinUI 3** interface (`EvolutionWin`).

## Quick start

### 1. Get the sources

At its root, this repository groups the source code of **Evolution** (`evolution/`) and
of **evolution-data-server (EDS)** (`evolution-data-server/`), as well as the
native WinUI 3 client (`winui/`) and the porting tools (`tools/`).

The upstream sources are versioned directly in this repository (monorepo):
a first commit imports Evolution (upstream commit `be33fb6204`) and EDS
(upstream commit `ea6b0124d`) as-is, and the next one applies the Windows
changes. `git diff <import>..<Windows commit> -- evolution` therefore gives the
complete patch. evolution-ews (upstream commit `19cb82b3`) is imported the
same way (`evolution-ews\.git-upstream` locally).

On the development machine, the upstream (GNOME) history of both projects
is kept locally in `evolution\.git-upstream` and
`evolution-data-server\.git-upstream` (ignored by Git, branch
`windows-native`), to track upstream:

```bat
git --git-dir=evolution-data-server\.git-upstream --work-tree=evolution-data-server fetch origin
```

The Windows changes to EDS are also kept as a
patch (`tools/patches/evolution-data-server.patch`, based on EDS commit
`ea6b0124d`). If EDS has to be re-cloned from upstream,
`tools\check-prereqs.bat -Install` clones it and applies this patch.

### 2. Check the prerequisites

```bat
tools\check-prereqs.bat            :: report only
tools\check-prereqs.bat -Install   :: installs/downloads what is missing
```

The script checks the tools (Visual Studio 2022 + C++, Windows SDK, Git,
CMake, Python and the `gvsbuild`/`meson`/`ninja`/`gyp-next` packages, MSYS2 with
`make`/`patch` and `intltool`/`perl`, WebView2 runtime), the sources (EDS, NSS, NSPR, libical, WebView2
SDK) and the state of the builds. With `-Install`, missing tools
are installed via `winget`/`pip` (Visual Studio Build Tools asks for
UAC elevation) and the sources are cloned/downloaded. It does not start any
build. Exit code: 0 if all mandatory prerequisites are
present, 1 otherwise.

### 3. Build, in this order

```bat
tools\build-gtk.bat         :: GTK3 stack via gvsbuild (1 to 2 h the first time)
tools\build-nss.bat         :: NSPR + NSS
tools\build-libical.bat     :: libical + libical-glib
tools\build-eds.bat         :: evolution-data-server
tools\build-ews.bat         :: evolution-ews (Microsoft 365 / Exchange, EDS parts)
tools\build-webkit-shim.bat :: WebKitGTK API on WebView2 (phase 3)
tools\build-evolution.bat   :: Evolution (transitional GTK version)
tools\build-winui-core.bat  :: evo-core.dll (C engine for the WinUI 3 client)
tools\build-winui-app.bat   :: EvolutionWin.exe (native WinUI 3 client, generated in .\build)
tools\run-winui.bat         :: launches .\build\EvolutionWin.exe with the right PATH
tools\build-msi.bat         :: installer dist\Evolution-<version>-x64.msi (phase 5)
```

## Directory layout

All scripts derive their paths from their own location
(`tools\env.bat`); no machine-specific path is hard-coded in them.

| Path | Role | Variable (overridable) |
| --- | --- | --- |
| `<workspace>` | root of this repository (`Evolution-WindowsPort`) | `EVO_WORKSPACE` |
| `<workspace>\winui` | native Windows 11 client (`winui\core`: `evo-core.dll`, `winui\app`: `EvolutionWin.exe`) | `WINUI_SRC` |
| `<workspace>\tools` | MSVC porting scripts and tools (`build-*.bat`, `env.bat`, `check-prereqs.ps1`, `cmake/`, `compat/`, `patches/`, `pkgconfig/`, `webkit-shim/`, `tools/nuget.exe`) | `MSVC_SCRIPTS` |
| `<workspace>\build` | ready-to-use final executables (`EvolutionWin.exe`, `evo-core.dll`, `evo-core-test.exe`, `evolution.exe`, `libexec\*.exe`) and `build\intermediate` subfolder (CMake/MSBuild object files, ignored by Git) | `BUILD_DIR` / `BUILD_INTERMEDIATE` |
| `<workspace>\evolution` | Evolution source code, `windows-native` branch | `EVO_SRC` |
| `<workspace>\evolution-data-server` | EDS source code, `windows-native` branch | `EDS_SRC` |
| `<workspace>\evolution-ews` | evolution-ews source code (Microsoft 365 / Exchange), `windows-native` branch | `EWS_SRC` |
| `<workspace>\deps` | third-party sources (NSS, NSPR, libical, WebView2 SDK) | `DEPS` |
| `<workspace>\gtk-build` | folder of the GTK stack built by gvsbuild (ignored by Git) | `GTK_BUILD` |
| `<workspace>\gtk-build\gtk\x64\release` | common installation prefix of all dependencies | `PREFIX` (derived from `GTK_BUILD`) |
| `C:\msys64` | MSYS2, used **only as a tool provider** (patch, make, bash) by gvsbuild and the NSPR build. No produced binary depends on MSYS2. | `MSYS2_ROOT` |

Visual Studio (2026 or 2022) is located automatically with `vswhere` (all editions).
The `VS_VERSION` variable selects the desired version (`set VS_VERSION=2026` or `set VS_VERSION=2022`); by default, the most recent installed version is used. The path can also be overridden directly via `VS_INSTALL`.

## Architecture choices

| Linux component | Windows replacement |
| --- | --- |
| WebKitGTK (`webkit2gtk-4.1`) — mail display, editor, previews | **Microsoft WebView2** (Edge/Chromium) embedded in WinUI 3 (`EvolutionWin`) and in a GTK widget (phase 3) |
| libsecret (Secret Service D-Bus) | **Windows Credential Manager** (`CredWriteW`/`CredReadW`/`CredDeleteW`) |
| libuuid | GLib's `g_uuid_string_random()` |
| D-Bus session bus (EDS services) | session bus built into GLib on Windows (`autolaunch:` address, started by `gdbus.exe`). It does not handle activation through `.service` files: EDS itself starts `evolution-source-registry` and the factories. |

## Tooling

| Tool | Tested version | Installation (`check-prereqs -Install`) |
| --- | --- | --- |
| Visual Studio 2026 / 2022, "Desktop development with C++" workload | MSVC 19.51 (VS 2026) / 14.44 (VS 2022) | `winget install Microsoft.VisualStudio.2022.BuildTools` or Visual Studio 2026 Community + VCTools workload |
| Windows SDK | 10.0.26100.0 | with Visual Studio |
| Git | 2.56 | `winget install Git.Git` |
| CMake (≥ 3.15) | 4.4.5 | `winget install Kitware.CMake` |
| Python (≥ 3.9) | 3.13.7 | `winget install Python.Python.3.13` |
| gvsbuild, meson, ninja, gyp-next | 2026.10.0, 1.12.1, 1.13.2 | `python -m pip install --user ...` |
| MSYS2 (+ `make`, `patch`) | 2026-09-27 | `winget install MSYS2.MSYS2` |
| Edge WebView2 runtime | 156.0.4314.50 | shipped with Windows 11 (otherwise `winget install Microsoft.EdgeWebView2Runtime`) |

## Phase 0 — GTK3 stack built with MSVC

Script: `tools\build-gtk.bat` (calls
`gvsbuild build --fast-build --vs-ver vs2022 --configuration release` with the
project list: gtk3, libsoup3, glib-networking, libxml2, sqlite, enchant,
json-glib, gsettings-desktop-schemas, adwaita-icon-theme, hicolor-icon-theme,
librsvg, pkgconf, icu, libarchive, openssl).

### Problems encountered

1. **`'create-lists.bat' is not recognized`** during the gettext build.
   Cause: the environment variable `NoDefaultCurrentDirectoryInExePath=1`
   prevents `cmd.exe` from running a script from the current directory, which the
   gettext nmake Makefiles rely on. Solution: `env.bat` clears this
   variable for all build scripts.

## Script organization (`tools/`)

Everything related to MSVC tooling and porting is grouped in `tools/` at
the root of the repository. The `evolution/win32/` folder contained an old
**MinGW** port (2014), abandoned for lack of WebKit2 on Windows (see
`evolution/win32/readme.txt`); it is not used.

| File | Role |
| --- | --- |
| `env.bat` | Common environment: relative paths, Visual Studio (vswhere), PATH, pkg-config |
| `check-prereqs.bat` / `check-prereqs.ps1` | Prerequisite check, automatic installation with `-Install` |
| `build-gtk.bat` | GTK3 stack and dependencies via gvsbuild |
| `build-nss.bat` + `build-nss.sh` | NSPR + NSS (also installs `nss.pc`/`nspr.pc`) |
| `build-libical.bat` | libical + libical-glib |
| `build-eds.bat` | evolution-data-server |
| `build-ews.bat` | evolution-ews (Microsoft 365 / Exchange), EDS parts only |
| `build-webkit-shim.bat` | WebKitGTK 4.1 shim on WebView2 |
| `build-evolution.bat` | Evolution (transitional GTK version) |
| `build-winui-core.bat` | `evo-core.dll` + `evo-core-test.exe` |
| `build-winui-app.bat` | `EvolutionWin.exe` (native WinUI 3 client in `.\build`) |
| `run-winui.bat` / `run-winui-debug.bat` | Launching `.\build\EvolutionWin.exe` (normal or with debug traces) |
| `build-msi.bat` + `build-msi.ps1`, `msi/Evolution.wxs` | Standalone MSI installer in `dist\` (WiX v5) |
| `cmake/msvc-compat.cmake` | CMake layer injected into all projects |
| `patches/evolution-data-server.patch` | Windows changes to EDS |
| `pkgconfig/*.pc.in` | pkg-config templates for NSS/NSPR/ICU/sqlite3 (`@PREFIX@` replaced at installation) |
| `nspr-config.site`, `tools/nsinstall`, `tools/patch-nspr.py`, `tools/nuget.exe` | Helpers for the NSPR build and NuGet restore |
| `webkit-api-usage.txt` | Inventory of the WebKit/JSC calls used by Evolution (phase 3) |

### Parallel working copies (Git worktrees)

Several copies of the repository can build the WinUI client at the same time
(`git worktree add`, parallel working branches):

- `evo-core` is no longer installed in the common prefix: it is built in
  `build\intermediate\evo-core` of each copy, and `EvolutionWin` links against it
  (`EvoCoreLibDir`); `evo-core.dll` is copied to `build\`;
- in a secondary worktree, `env.bat` reuses the GTK stack (`gtk-build`)
  and the NuGet packages (`WINUI_PACKAGES`) of the main repository
  (`git rev-parse --git-common-dir`), which are large and outside Git;
- the interface preferences are in
  `%LOCALAPPDATA%\EvolutionWin\settings.json` (`winui\app\Settings.h`).

To test the engine from several copies at once without sharing the
account's Camel cache, set `XDG_DATA_HOME` and `XDG_CACHE_HOME` to
folders specific to each copy.

### CMake compatibility layer

GNOME projects pass the raw pkg-config `*_LDFLAGS`
(`-L/dir -lglib-2.0`) to `target_link_libraries()`. With MSVC, CMake
forwards them as-is to `link.exe`, which ignores them (`LNK4044`), hence unresolved
symbols. `msvc-compat.cmake` redefines `target_link_libraries()`
to translate `-L<dir>` into `-LIBPATH:<dir>` and `-l<name>` into `<name>.lib`.
It is loaded with `-DCMAKE_PROJECT_INCLUDE=...`: no CMakeLists is
modified for this.

## Phase 1 — evolution-data-server

### Changes to the EDS sources (`windows-native` branch)

They are exported to `tools/patches/evolution-data-server.patch`.
After each change to EDS, regenerate the patch from the repository root:
`git diff --relative=evolution-data-server <import commit> -- evolution-data-server > tools\patches\evolution-data-server.patch`
(`<import commit>`: the import commit of the upstream sources, see "Quick start").

- `src/libedataserver/e-secret-store.c`: the historical Win32 implementation
  stored passwords base64-encoded (hence in clear text) in
  `%LOCALAPPDATA%\evolution\secrets`. It is replaced by the Windows
  Credential Manager (targets `Evolution-Data-Server:<uid>`, type
  `CRED_TYPE_GENERIC`, persistence `CRED_PERSIST_LOCAL_MACHINE`, DPAPI
  protection tied to the user). Session secrets stay in memory.
  The old file is read as a fallback, then migrated automatically.
- `src/libedataserver/e-ms-oapxbc.c`: libuuid replaced by
  `g_uuid_string_random()` under `G_OS_WIN32`.
- `CMakeLists.txt`: `uuid` and `libsecret-1` are no longer required under `WIN32`.

### CMake configuration problems (EDS, and the same in Evolution)

| Error | Cause | Fix |
| --- | --- | --- |
| `zlib.h not found` | the `check_include_file`/`check_library_exists` tests do not see the prefix | `env.bat` adds the prefix to the `INCLUDE` and `LIB` variables read by `cl`/`link` |
| `zlib doesn't provide deflateEnd` | gvsbuild names the library `zlib1.lib`, EDS looks for `z` | `build-gtk.bat` creates `z.lib` |
| `Failed to find icui18n…` | gvsbuild does not install `icu-*.pc`; on Windows the libraries are called `icuin`/`icudt` | templates `icu-uc.pc.in`, `icu-i18n.pc.in` |
| `sqlite3>=3.7.17 not found` (and `enchant-2`, `gsettings-desktop-schemas`, `libicalvcal`) | `.pc` files missing | corresponding templates, installed by `install-pc.bat` |
| `You need to install a working iconv` | `CMAKE_REQUIRED_LIBRARIES "-liconv"` passed as-is to `link.exe` | bare name `iconv` (CMake translates it to `-liconv` or `iconv.lib`), portable |
| `Invalid character escape '\U'` in `CHECK_C_SOURCE_RUNS` | `file(TO_NATIVE_PATH)` produces `\` inserted in a C string | paths in CMake format (`/`), also valid in C on Windows |
| "Debug Assertion Failed" window during configuration | the test programs are built in Debug (debug CRT); `strftime("%lx%k")` triggers a modal assertion | `CMAKE_TRY_COMPILE_CONFIGURATION=Release` in `msvc-compat.cmake` |
| `Unable to find a way to determine timezone` | MSVC has neither `tm_gmtoff` nor the POSIX `timezone` variable | new `HAVE__TIMEZONE` detection; `camel-mime-utils.c` and `e-time-utils.c` use `_get_timezone()`/`_get_dstbias()` (also handles a daylight saving offset ≠ 1 h) |
| `System doesn't support necessary interfaces for IPv6` | test written for POSIX headers (`sys/socket.h`, `netdb.h`) | test with `winsock2.h`/`ws2tcpip.h` + `ws2_32` on Windows; IPv6 stays enabled |

### Compilation and linking problems (EDS)

General fixes in `tools/cmake/msvc-compat.cmake` (also valid
for Evolution):

| Problem | Fix |
| --- | --- |
| `gdbus-codegen`, `glib-mkenums`, `glib-genmarshal` "not recognized" | these are Python scripts without an extension: cache variables preset to `python;<script>` (`find_program()` keeps them) |
| ICU 78: `nested-namespace-definition requires /std:c++17` | `CMAKE_CXX_STANDARD 17` |
| Non-ASCII literals misread (ANSI code page) | `/utf-8` |
| `unistd.h`, `dirent.h`, `regex.h`, `utime.h`, `sys/time.h`, `sys/param.h` missing | minimal headers in `tools/compat/include`: CRT functions under their POSIX name, `dirent` on `FindFirstFileW` (UTF-8), `regex` on PCRE2-POSIX, `ftruncate` → `_chsize_s` |
| `mode_t`, `pid_t`, `ssize_t`, `S_IRWXU`, `S_ISDIR`, `O_ACCMODE`, `localtime_r`, `gmtime_r` | preliminary header `msvc-prelude.h` injected everywhere (`/FI`) |
| C2011 "struct redefinition" (`winsock.h` versus `winsock2.h`) | `msvc-prelude.h` defines `_WINSOCKAPI_`: `windows.h` no longer includes the old `winsock.h` |
| DLL without an import library (`edbus-private.lib` not found) | `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON` (MSVC exports nothing by default, unlike ELF) |
| Plugins loaded but "no initialization code in the module" | automatic export does not apply to `MODULE` libraries: `add_library(... MODULE ...)` is redirected to `SHARED` with the `lib` prefix (names identical to Linux, expected by the loaders) |

Fixes in the EDS sources (in the patch):

| File | Problem | Fix |
| --- | --- | --- |
| 61 module files (EDS + Evolution) | C2375: `e_module_load` prototype without `G_MODULE_EXPORT`, definition with | `G_MODULE_EXPORT` added to the prototype (`tools/tools/fix-module-export-prototypes.py`, no effect under GCC) |
| `camel-enums.h`, `camel.h`, `camel-mime-utils.h`, `camel-provider.h`, `camel-debug.h` | global variables shared between DLLs (`camel_application_is_exiting`, …): MSVC requires `__declspec(dllimport)` on the user side | `CAMEL_VAR` macro; `camel_verbose_debug` declared publicly in `camel-debug.h` and initialized (a *common* symbol is not exported) |
| `camel-msgport.c` | `#ifdef G_OS_WIN32` before including glib: `winsock2.h` never included | `#ifdef _WIN32` |
| `camel-mime-utils.c`, `e-data-server-util.c` | `getgid()`/`getppid()` (Message-ID / UID randomness) | excluded under Windows |
| `camel-gpg-context.c` | `local_error` not declared in the Windows branch | declared (OpenPGP port: phase 4c) |
| `e-book-sqlite.c` | GNU variadic macro `args...` | C99 syntax `...` / `__VA_ARGS__` |
| `gen-western-table.c` | CRLF input: `\r` in generated identifiers | trailing `\r` removed |
| `evolution-alarm-notify/CMakeLists.txt` (and `src/shell` of Evolution) | icon built by `windres` (MinGW) | under MSVC, the `.rc` is added to the sources (compiled by `rc.exe`) |
| `e-data-server-util.c` | `system ("dbus-launch.exe")` inherited from MinGW (console window) | removed: GLib starts its own session bus |

### Execution

- `build-eds.bat` moves the EDS DLLs from `lib\` to `bin\` (EDS
  installs them the Unix way; Windows looks for them next to the executable or
  in the PATH), compiles the GSettings schemas (`glib-compile-schemas`) and
  copies the executables from `libexec\` to `.\build\libexec\`.
- **Test passed (2026-10-31)**: `libexec\evolution-source-registry.exe`
  starts, the GLib session bus (`gdbus.exe`) is launched automatically,
  `list-sources.exe` lists the sources via D-Bus, and Camel loads its five
  providers (IMAP, POP3, SMTP, local, NNTP), without errors or warnings.

### Additional dependencies to build (outside gvsbuild)

Sources in `<workspace>\deps` (downloaded by `tools\check-prereqs.bat -Install`):

| Project | Origin | Build |
| --- | --- | --- |
| NSS (3.133 beta) | `git clone --depth 1 https://github.com/nss-dev/nss.git` | `tools\build-nss.bat` (gyp-next + ninja, MSVC) |
| NSPR 4.38 | `https://ftp.mozilla.org/pub/nspr/releases/v4.38/src/nspr-4.38.tar.gz` | built by `nss/build.sh` (autoconf, cl.exe) |
| libical 3.0.20 | `git clone --branch v3.0.20 https://github.com/libical/libical.git` | `tools\build-libical.bat` (CMake + Ninja) |

`gyp-next` is installed via `python -m pip install --user gyp-next`.

#### Problems encountered with NSS/NSPR

1. **`Can't find vswhere on the path`**: `nss/build.sh` locates MSVC itself;
   `C:\Program Files (x86)\Microsoft Visual Studio\Installer` is added to the PATH.
2. **`configure: error: Could not determine MSC version`**: the French
   `cl.exe` prints `version<0xFF>19.44…` (CP850 non-breaking space). The sed
   filter of `nspr/configure` required a space; it now accepts any non-numeric
   separator. The fix is applied automatically (and
   idempotently) by `tools\tools\patch-nspr.py`, launched from
   `build-nss.sh`. The locale
   is forced to `C` (`LC_ALL=C`) because the 0xFF byte is not valid UTF-8.
   `VSLANG=1033` has no effect if the English language pack is not installed.
3. **MSYS2 declares itself `x86_64-pc-cygwin`**: NSPR must see a mingw
   target to choose its WINNT/WIN95 paths with cl.exe. `tools\nspr-config.site`
   sets `ac_cv_build/host/target=x86_64-pc-mingw32`; `CONFIG_SITE` is
   re-exported *inside* the bash shell because the MSYS2 profile redefines it
   (`tools\build-nss.sh`).
4. **`nsinstall: not found`**: tool provided by MozillaBuild;
   replaced by a minimal script `tools\tools\nsinstall`.
5. **`link: unknown option -- n`**: the coreutils `/usr/bin/link` (MSYS2)
   shadowed MSVC's `link.exe`; the `cl.exe` folder is placed at the front of the PATH.
6. **`certdata.py` → `FileNotFoundError`**: generating `certdata.c`
   launches `perl` from Python (hence outside MSYS2). We use the native Perl
   downloaded by gvsbuild (`%GTK_BUILD%\tools\perl-5.20.0\x64\bin\perl.exe`),
   passed through the `PERL` variable defined in `env.bat`.

Result: `nspr4`, `plc4`, `plds4`, `nss3`, `nssutil3`, `smime3`, `ssl3`,
`softokn3`, `freebl3`, `nssckbi` (DLLs) installed in
`%GTK_BUILD%\gtk\x64\release\bin`. NSS names its import libraries
`nss3.dll.lib`: copies `nss3.lib`, `nssutil3.lib`, `smime3.lib`,
`ssl3.lib` are created by `build-nss.bat` so that `-lnss3` works. The
`nss.pc`/`nspr.pc` files, absent on Windows, are generated by
`build-nss.bat` from the templates `tools\pkgconfig\*.pc.in`.

#### Problems encountered with libical

1. **`LNK4044: unrecognized option '/lglib-2.0'`**: solved by
   `msvc-compat.cmake` (see "CMake compatibility layer").
2. **`error C2143` in the generated `i-cal-*.h` headers**: the repository had
   been cloned with `core.autocrlf=true`. The generator templates were in
   CRLF and the generator, which writes in text mode, produced `\` followed by
   `\r\r\n`: the macros' line continuations were broken.
   Fix: libical sources reset to LF (`git config core.autocrlf false`
   then `git rm --cached -r . && git reset --hard`).
3. Since Windows has no `zoneinfo` database, libical is built with
   `USE_BUILTIN_TZDATA=ON` (time zones installed in `share\libical\zoneinfo`).

## Phase 2 — Evolution

Script: `tools\build-evolution.bat`. Optional features
disabled (Linux desktop dependencies): gnome-desktop, canberra,
libnotify, gweather, autoar, ytnef, PST import, markdown (cmark),
syntax highlighting (highlight), yelp help, LDAP. For LDAP, Windows
provides `wldap32`: a lead for enterprise directories, later.

Configuration:

| Problem | Fix |
| --- | --- |
| `intltool-update not found` | `pacman -S intltool` in MSYS2 (build tool only); Perl scripts run through MSYS2's `perl.exe` (cache variables preset in `msvc-compat.cmake`, `MSYS2_ROOT` defined in `env.bat`) |
| `shared-mime-info>=0.22` | `shared-mime-info.pc.in` template (data only: on Windows GLib uses the registry for content types) |
| `webkit2gtk-4.1>=2.40.0` | **expected blocker** → WebView2 shim (phase 3) |

As for EDS, `TO_NATIVE_PATH`, the `iconv` test and the time zone test
of Evolution's `CMakeLists.txt` were fixed the same way.

### Linking and first launch

| Unresolved symbol | Cause | Fix |
| --- | --- | --- |
| `camel_mime_special_table` | `const` data from a generated file: ignored by automatic export | `#pragma comment(linker, "/EXPORT:camel_mime_special_table,DATA")` in `camel-mime-utils.c` (EDS) |
| `camel_header_param_encode_filenames_in_rfc_2047` | declared by hand (`extern`) in `mail-config.c` | public `CAMEL_VAR` declaration in `camel-mime-utils.h`; the local `extern` is kept only if `CAMEL_VAR` does not exist |
| `is_any_gettext_catalog_installed` | defined in `main.c` (executable) but called by the shell DLL | moved to `e-shell.c` |
| `_e_get_data_server_icondir` | the header declared a different name from the one generated by `GETTER(e_data_server_icondir)` | header aligned: `_e_get_e_data_server_icondir` |
| `context` (virtual folders) | global variable of `libemail-engine` used by `evolution-mail` | accessor `mail_vfolder_get_context ()` |

`build-evolution.bat` moves Evolution's private DLLs (`lib\*.dll`) to
`bin\`, compiles the GSettings schemas and copies `evolution.exe` as well as its
DLLs to `.\build\`. The configuration is stored by GLib in the Windows
registry (`HKCU\Software\GSettings`).

**First successful launch (2026-10-31)** — after manually starting
`libexec\evolution-source-registry.exe`: "Mail" main window,
local folders, French interface, no errors (only expected warning:
no `sendmail` provider on Windows). Screenshot:
`tools/screenshots/2026-10-31-first-run.png`.

### Phase 2b — automatic start of the EDS services (done)

GLib's session bus on Windows does not activate services from
`.service` files. New public function (Windows only)
`e_util_win32_ensure_dbus_service (bus_name, cancellable, error)` in
`libedataserver`:

1. checks with `NameHasOwner` whether the D-Bus name has an owner;
2. if not, launches the matching executable from `libexec\` (relocated
   path, `E_DATA_SERVER_LIBEXECDIR`) with `CreateProcessW` and
   `CREATE_NO_WINDOW` (no console);
3. waits (30 s maximum) for the service to own its name; does nothing if
   called by the service itself.

Mappings: `Sources5` → `evolution-source-registry.exe`,
`AddressBook10` → `evolution-addressbook-factory.exe`, `Calendar8` →
`evolution-calendar-factory.exe`, `UserPrompter0` →
`evolution-user-prompter.exe`. Called just before the D-Bus proxies are
created in `e-source-registry.c`, `e-book-client.c`, `e-cal-client.c` and
`e-user-prompter.c` (already executed in threads).

The predefined "Sendmail" source is no longer loaded on Windows
(`e-source-registry-server.c`): no sendmail and no Camel provider.

**Validated (2026-10-31)**: launching only `evolution.exe` starts the bus,
the registry, then the address book factory, with no console window; a
real IMAP account configured by the user synchronizes its folders and
displays the message list. Displaying HTML content awaits phase 3.

The services inherit the client's `PATH` to find their DLLs. For
EvolutionWin, `evo-core` itself places the folder of `evo-core.dll` (`bin\`)
at the front of the `PATH` at startup (phase 5): no system setting required.

## Phase 3 — preparation: WebKit shim on WebView2

### Strategy

`EWebView` inherits from `WebKitWebView` and Evolution uses the WebKit API
in ~1500 places. Rather than rewriting these calls, we provide a
**compatibility library** (`webkit2gtk-4.1` on the pkg-config side, headers
`webkit2/webkit2.h`) that implements on WebView2 only the subset of the
API actually used: about 110 WebKit and JSC functions/types.
The exact inventory (number of occurrences per symbol) is in
`tools/webkit-api-usage.txt`.

Main mappings:

| WebKitGTK | WebView2 |
| --- | --- |
| `webkit_web_view_load_html/uri/bytes` | `NavigateToString` / `Navigate` |
| `webkit_web_view_evaluate_javascript` + `JSCValue` | `ExecuteScript` (JSON result → emulated `JSCValue`) |
| `webkit_user_content_manager_register_script_message_handler` | `window.chrome.webview.postMessage` + `WebMessageReceived` |
| `webkit_web_context_register_uri_scheme` (`evo-http`, `cid`, `mail`, …) | `AddWebResourceRequestedFilter` + `WebResourceRequested` |
| `decide-policy` | `NavigationStarting` / `NewWindowRequested` |
| `WebKitFindController` | `window.find()` via script |
| `WebKitPrintOperation` | `ICoreWebView2_16::Print*` |

### Step 1 done: shim skeleton (`tools/webkit-shim`)

- A single DLL `webkit2gtk-4.1.dll` provides the WebKitGTK 4.1 API and the
  JavaScriptCore-GLib API; files `webkit2gtk-4.1.pc` and
  `javascriptcoregtk-4.1.pc` (version 2.44.0). Headers in
  `include\webkit2gtk-4.1\{webkit2,jsc}`. Script: `tools\build-webkit-shim.bat`.
- `WebKitWebView` derives from `GtkContainer`; its class structure is
  public (`EWebView` and `EWebKitEditor` embed it). The signals and
  properties that Evolution uses are registered with their real
  signatures (`load-changed`, `decide-policy`, `context-menu`,
  `script-message-received::*`, `initialize-web-extensions`, `found-text`,
  `finished`, properties `title`, `editable`, `zoom-level`, 60 `WebKitSettings`
  properties…).
- `JSCValue` is based on a json-glib node (WebView2 returns JSON).
- Implementation for now **empty**: each point to be implemented is marked
  `/* TODO WebView2 */`; asynchronous calls fail with
  `G_IO_ERROR_NOT_SUPPORTED`.
- Evolution: `src/web-extensions`, `src/modules/webkit-editor/web-extension`
  and `src/modules/webkit-inspector` are not built on Windows (no
  extension in the render process with WebView2).
- Fix in `cmake/modules/GLibTools.cmake` (Evolution): an absolute path
  was used as a variable name (`${C:/…/x.gresource_DEPS}`), a CMake syntax
  error on Windows; replaced with a fixed name.

### Step 2 done: WebView2 rendering (2026-10-31)

`tools/webkit-shim/src/webview2-host.{h,cpp}` (C++/WRL, C API):
shared WebView2 environment (user folder
`%LOCALAPPDATA%\evolution\webview2` via the GLib cache, static loader
`WebView2LoaderStatic.lib`), one controller per view in a native child
window (`gdk_window_ensure_native`), queue of pending commands during
asynchronous creation.

| WebKitGTK | Implementation |
| --- | --- |
| `load_html` / `load_bytes` / `load_plain_text` | content served by the internal scheme `webkit-shim-page:` (no 2 MB limit, unlike `NavigateToString`) |
| Evolution schemes (`mail`, `cid`, `evo-file`, `evo-http(s)`, `gtk-stock`) | declared when the environment is created (`CustomSchemeRegistration`), requests via `WebResourceRequested` → `webkit_web_context_register_uri_scheme()` handlers, response read asynchronously then `CreateWebResourceResponse` |
| `http(s)` sub-resources of Evolution pages | rewritten to `evo-http(s)` as the web extension did (Evolution's remote content policy) |
| `evaluate_javascript` | `ExecuteScript` of a wrapped global `eval`: exceptions → `WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED`, JSON value → `JSCValue` |
| `window.webkit.messageHandlers.<name>.postMessage` | defined by a "page script" injected into every document and frame, relayed by `chrome.webview.postMessage` (including the frame event) |
| user style sheets | `<style id="webkit-shim-user-styles">` updated on every change |
| `load-changed`, `decide-policy`, `context-menu`, `mouse-target-changed` | `NavigationStarting`/`ContentLoading`/`NavigationCompleted`, `NewWindowRequested`, `ContextMenuRequested` (synthesized GDK event), `StatusBarTextChanged` |

On the Evolution side (`e-web-view.c`, Windows): `e-convert.js`, `e-web-view.js` and
the `preview-plugins` plugins are added as a user script of the main
frame (replaces the web extension); `Evo.getUriTooltip` and
`EvoConvert.linkRequiresReference` are approximated in JavaScript.

Problems encountered:

1. **"Ã€" instead of "À"**: text served without `charset` read as
   Windows-1252 → `charset=utf-8` added to `text/*` types.
2. **Style sheet ignored**: GIO on Windows gives content types as an
   extension (`.css`); WebView2 refuses a CSS without the
   right MIME type → `g_content_type_get_mime_type()` conversion (+ fallback
   table).
3. **Message body invisible (zero-height iframe)**: the script of
   the main page (`webkit-shim-page:`) reads the content of the `mail:`
   iframes; different origins for Chromium. WebKitGTK treats these local
   schemes as trusted: the WebView2 environment is launched with
   `--disable-web-security --disable-site-isolation-trials`. Accepted
   trade-off for the transitional GTK version (remote content is still filtered
   by Evolution); the new WinUI interface will serve everything under a single
   origin.

Diagnostics: `WEBKIT_SHIM_DEBUG=1` traces navigations, requests, responses,
scripts and JavaScript errors of the pages.

**Validated**: text and HTML messages (layout, web fonts, local
images) displayed in the preview pane, with a real IMAP account.

Known limitations (transitional version, non-priority): "Evo is not defined"
errors on the initial empty document (harmless),
Evolution keyboard shortcuts inactive when focus is in the page,
in-page search, printing, composer editor (phase 4 not
continued for GTK).

### Other ports on the Evolution side

- `src/e-util/e-passwords.c` (old password API): on Windows,
  EDS's `e_secret_store_*()` (Credential Manager) instead of
  libsecret; libsecret is no longer required under `WIN32` (Evolution, and
  EDS's `libedataserver-1.2.pc`).
- bogofilter/spamassassin spam filters disabled (driving external programs
  through `fork()`/`waitpid()`): to be ported.
- `certificate-manager.c`: variable-length array (not supported by
  MSVC) replaced by `g_newa`. `e-shell.c`: missing `windows.h`.
- `msvc-compat.cmake`: `-lm`/`-ldl` ignored (provided by the CRT).

### Web extension (WebKit process)

`src/web-extensions/e-web-extension.c` only does:

1. load `e-convert.js`, `e-web-view.js` then the JS plugins (data in
   `data/webkit`) on each creation of the JS context;
2. expose two synchronous native functions (`Evo.getUriTooltip`,
   `EvoConvert.linkRequiresReference`);
3. rewrite `http(s)` requests to `evo-http(s)` so they go
   through Evolution.

Under WebView2: (1) `AddScriptToExecuteOnDocumentCreated`, (2)
reimplementation in JavaScript, (3) `WebResourceRequested`. No native code
in the render process is needed.

SDK: `Microsoft.Web.WebView2` 1.0.4314.35 (NuGet) in
`<workspace>\deps\webview2\sdk`. Edge WebView2 runtime 156 already
present on Windows 11.

## Phase 4b — Exchange Online / Microsoft 365 (goal: replace Outlook)

### Protocol: Microsoft Graph, not EWS

Microsoft is retiring EWS from Exchange Online: automatic blocking of tenants not
configured since **October 1, 2026**, permanent removal on
**April 1, 2027**, with no exceptions. Only on-premises Exchange Server keeps EWS.
The target is therefore the **Microsoft Graph** API.

The **evolution-ews** module (separate repository,
`https://gitlab.gnome.org/GNOME/evolution-ews.git`) provides two account
types:

| Account type | Protocol | Use in this port |
| --- | --- | --- |
| "Microsoft 365" | Microsoft Graph | **Exchange Online** (mail, calendar, contacts) |
| "Exchange Web Services" | EWS | On-premises Exchange only |

Known limitations of Graph compared with EWS: no access to public
folders, partial coverage of archive mailboxes.

### OAuth2 authentication (Entra ID)

Basic authentication is disabled on Exchange Online: sign-in
goes through OAuth2. Points to handle:

1. **Sign-in window**: on Linux, it uses WebKitGTK. On
   Windows, it will go through WebView2, which also supports modern
   authentication, MFA, conditional access. Possible variant:
   system browser + loopback redirect (RFC 8252).
2. **Entra ID application registration**: evolution-ews provides a default
   client ID, but many tenants require admin
   consent. For an enterprise deployment, plan for
   an application registration in the tenant, with delegated permissions
   `Mail.ReadWrite`, `Mail.Send`, `Calendars.ReadWrite`,
   `Contacts.ReadWrite`, `User.Read`, `offline_access`. The client ID
   and tenant will be pre-configurable at installation (MSI properties,
   phase 5), for a deployment without user input.
3. **Tokens**: stored in the Windows Credential Manager (same
   mechanism as passwords, see phase 1).

### OpenPGP (GPG)

`camel-gpg-context.c` launches `gpg` with `fork()`/`pipe()`/`fcntl()`; the
Windows branch was only a `FIXME: Port me`. It is now ported to
`g_spawn_async_with_pipes()` with Gpg4win (`gpg.exe`): see the eighth
milestone of the native client. S/MIME, which goes through NSS, is not concerned.

### Steps

- [x] Build evolution-ews with MSVC (2026-10-31)
- [x] OAuth2 window on WebView2 in EvolutionWin (2026-10-31)
- [ ] End-to-end test on an Exchange Online tenant: mail, calendar, contacts

### Building evolution-ews

Sources in `<workspace>\evolution-ews` (GNOME repository, upstream commit
`19cb82b3`, `windows-native` branch). Script: `tools\build-ews.bat`
(after `build-eds.bat`), with `WITH_EVOLUTION=OFF`: only the EDS parts
are built (Camel providers `ews` and `microsoft365`, address book
and calendar backends, registry modules, private libraries
`evolution-ews.dll`, `evolution-microsoft365.dll`, `camel*-priv.dll`,
`evolution-ews-common.dll`, moved to `bin\`). The GTK interface
configuration pages are not used by EvolutionWin.
`WITH_MSPACK=OFF`: no libmspack (decompression of the EWS offline
address book, OAB).

Fixes: `G_MODULE_EXPORT` prototypes (C2375) by
`tools\tools\fix-module-export-prototypes.py`, whose pattern confused
`x = function (...);` with a prototype (fixed: no `=` before the name);
`e-cal-backend-ews-utils.h` declares with `G_MODULE_EXPORT` the three
functions exported for the tests.

### Microsoft 365 accounts in EvolutionWin

The Microsoft 365 connector gets its access tokens through
`e_source_get_oauth2_access_token_sync`: the EDS registry renews them
from the stored refresh token (`OAuth2::Microsoft365[<address>]`
in EDS's secret store, hence the Windows Credential
Manager). Only the **initial sign-in** is interactive; on Linux it
goes through an EDS GTK/WebKit dialog, replaced here by EvolutionWin.

evo-core:

- loads at startup, before the registry, the OAuth2 modules
  (`module-microsoft365-backend`, `module-ews-backend`,
  `module-oauth2-services`): `ESourceRegistry` finds the services there;
- `evo_core_add_microsoft365_account`: sources created as by Evolution's
  wizard, one collection (backend `microsoft365`, authentication
  `Microsoft365`, settings `CamelM365Settings`) carrying the mail account,
  identity and submission server; the registry module adds the calendars
  and address books (Graph) to it. Application ID and tenant
  optional (Evolution's application `20460e5d-…` and tenant `common` by
  default);
- `evo_core_oauth2_begin` / `evo_core_oauth2_finish`: same construction as
  the EDS dialog (service parameters, `state` checked on return, refusal
  `error_description` passed up), code exchanged and tokens stored by
  `e_oauth2_service_receive_and_store_token_sync`;
- `oauth2-required` event when the connector refuses authentication
  (missing or revoked token; not on a network error);
- removal: the whole collection (calendars and address books included) and the
  stored token.

EvolutionWin: "Microsoft 365 / Exchange Online" choice in the add-account
wizard (no servers or password), then a WebView2 sign-in window; navigation to the redirect
address
(`…/oauth2/nativeclient`) is intercepted and passed to the engine. Same
window on `oauth2-required`.

**Tested** (2026-10-31) with a fictitious account, created then deleted in the
real registry: creation, sign-in address (v2.0 endpoint, Graph
mail/calendar/contacts/tasks permissions, `offline_access`), user
refusal, forged response (different `state`) rejected, removal
with nothing left behind. **Still to validate** with a real Microsoft 365 account:
sign-in, fetch, send, calendars and contacts; an enterprise tenant may
require administrator consent for the Evolution application
or its own registration (ID and tenant in the wizard).

Not done: EWS accounts (on-premises Exchange, `ews`) in the wizard; the
providers and backends are built and installed.

## Phase 5 — MSI installer

Goal: a standalone `.msi` package, silently deployable on a machine
(`msiexec /i Evolution-x64.msi /qn`), via GPO, Intune or SCCM, containing
EvolutionWin, EDS and **all** their dependencies. Nothing must be required on
the machine apart from Windows 11.

### Implementation (2026-10-31)

```bat
tools\build-msi.bat                    :: rebuilds EvolutionWin in the tree, then the MSI
tools\build-msi.bat -Version 1.0.0     :: forced version (default: VERSION file, or EVO_MSI_VERSION)
tools\build-msi.bat -SkipAppBuild      :: reuses .\build\ (output of build-winui-app.bat)
```

Result: `dist\Evolution-<version>-x64.msi` (≈ 42 MB, 1,199 files,
149 MB installed; `dist\` is ignored by Git, versions are published
to GitHub Releases by `tools\publish-release.bat`). Files: `tools\build-msi.bat` (environment),
`tools\build-msi.ps1` (tree, dependencies, signing, WiX),
`tools\msi\Evolution.wxs` (WiX source).

Installed tree (`%ProgramFiles%\Evolution`), in prefix layout:
EDS and Camel compute their paths from their DLLs, GLib finds its GIO modules
and its schemas from its own, libical its time zones from the executable.

| Folder | Contents |
| --- | --- |
| `bin\` | `EvolutionWin.exe` and the self-contained Windows App SDK, `evo-core.dll`, 43 prefix DLLs (PE imports), dynamically loaded NSS (`softokn3`, `freebl3`, `nssckbi`), `gdbus.exe`, `gspawn-win64-helper*.exe`, Visual C++ runtime (local copy) |
| `libexec\` | `evolution-source-registry`, address book and calendar factories (and their subprocesses), `camel-index-control`, `camel-gpg-photo-saver` |
| `lib\evolution-data-server\` | Camel providers, address book/calendar backends, registry modules |
| `lib\gio\modules\` | `gioopenssl` (TLS: IMAPS/SMTPS), `giognomeproxy` |
| `share\` | `gschemas.compiled`, libical time zones, translations (EDS, GLib, glib-networking, libsoup) |

Choices and problems encountered:

1. **GTK excluded** (65 → 43 DLLs, 47 → 42 MB): only `evolution-user-prompter`
   and `module-trust-prompt` (EDS GTK dialogs) and the RSS provider
   (Evolution plugin) imported it. EvolutionWin itself asks for passwords and
   certificates; limitation: a confirmation requested by an online
   address book/calendar backend (CalDAV, CardDAV) fails instead of showing a
   dialog. The transitional GTK version (`evolution.exe`) is not in the MSI.
2. **EDS services without DLLs next to them**: `evo-core` adds its folder at the front of the
   `PATH` at startup; the services launched from `libexec\` inherit it.
3. **ICE validation** (`wix msi validate`, run by the script):
   - ICE03: the Windows App SDK `.mui` files carry recent languages
     (1152, 1153, 1169) unknown to the ICE tables; harmless, ignored;
   - ICE43/ICE57: Start menu shortcut of a per-machine package
     (`ALLUSERS=1`) assumed to be per-user; ignored. The shortcut is
     carried by the `EvolutionWin.exe` file (removed along with it);
   - ICE61: reinstall at equal version allowed (test builds).
4. **Launch condition**: WebView2 runtime present (EdgeUpdate key,
   version different from `0.0.0.0`).
5. **Update while Evolution is running** (0.9.1 over 0.9.0, VM): the
   "files in use" dialog stayed hidden behind
   Explorer, and the relaunched MSI failed with error 1618 ("another
   installation is in progress"). Since 0.9.2, `util:CloseApplication`
   (extension `WixToolset.Util.wixext`) closes EvolutionWin (close
   message, then stop after 5 s) and stops the EDS services and `gdbus.exe`
   before the files are replaced. On a development machine, this
   also stops the EDS services launched from the prefix (same process
   name); EvolutionWin restarts them as needed.

**Validated**: MSI extraction (`msiexec /a`) and execution from the
extracted tree, with a `PATH` reduced to the system: D-Bus bus and
EDS registry started from the package, IMAPS connection, the user's account, folders and
messages listed; `EvolutionWin.exe` opens its window and
starts the package's address book and calendar factories. The
actual installation (`msiexec /i`, UAC elevation) remains to be done by the user.

Still to do: application icon, registration as a mail client
and `.eml`/`.ics`/`.vcf` associations (once EvolutionWin can
open a file or a `mailto:` URL passed as an argument), language choice,
Gpg4win as an optional prerequisite.

### Tool

**WiX Toolset v5** (reference tool for MSIs, versionable XML sources,
command-line build):
`dotnet tool install --global wix --version 5.0.2`, checked and installed by
`check-prereqs.ps1` (optional prerequisite).

### Package contents

| Item | Origin | Note |
| --- | --- | --- |
| `EvolutionWin.exe`, `evo-core.dll`, `evolution.exe`, modules, plugins | `./build` | |
| EDS services (`evolution-source-registry`, address book/calendar factories, `evolution-alarm-notify`) | EDS build | started by EDS (no D-Bus activation on Windows) |
| Dependency DLLs (GLib, GTK3, libsoup, NSS, libical…) | prefix `%GTK_BUILD%\gtk\x64\release\bin` | **only those actually imported**, determined by walking the PE imports (`dumpbin /dependents`) starting from the executables and modules, so as not to ship the whole prefix |
| GIO modules (`glib-networking`: TLS for IMAPS/SMTPS) | `lib\gio\modules` | essential for encrypted connections; `giomodule.cache` regenerated at build |
| gdk-pixbuf loaders (PNG, SVG via librsvg…) | `lib\gdk-pixbuf-2.0` | `loaders.cache` generated at build with relative paths |
| GSettings schemas | `share\glib-2.0\schemas` | `gschemas.compiled` precompiled at build |
| Adwaita/hicolor icon themes, Evolution data (`share\evolution`), libical time zones | `share\…` | |
| Translations (`share\locale`) | builds | "languages" option in the installer |
| Visual C++ runtime | Visual Studio (`VC\Redist\MSVC`) | *app-local* copy of `vcruntime140*.dll`/`msvcp140.dll` in the installation folder: no system prerequisite |
| WebView2 runtime | included in Windows 11 | checked at installer launch (MSI condition); optionally, WiX/Burn bundle with the Evergreen bootstrapper for machines that lack it |

The whole application is relocatable: data paths are computed
at startup relative to the executable (`e-util-private.h`,
`e_util_win32_initialize()` on the EDS side), so installation can be done in
any folder.

### Windows integration

- Per-machine installation in `%ProgramFiles%\Evolution`, Start menu shortcut.
- Registration as a mail client (`mailto:` protocol, "Default
  apps"), `.eml`, `.ics`, `.vcf` associations.
- Fixed `UpgradeCode` → clean major upgrades (`MajorUpgrade`),
  full uninstallation through "Installed apps".
- User data in `%LOCALAPPDATA%\evolution` (not removed on
  uninstall).
- Authenticode signing of the MSI and binaries (`signtool`): **planned, done
  later** (decision of 2026-10-31). `build-msi.bat` will have an optional
  signing step, enabled only if a certificate is provided
  (variables `SIGN_CERT` / `SIGN_TIMESTAMP_URL`), and will otherwise produce an unsigned
  MSI usable for testing.

## Phase 6 — Native Windows look and behavior (requested on 2026-10-31)

Goal: make Evolution look like a Windows application and not like a
port of GNOME (windows, buttons, lists, menus, dialogs).

Technical observation: Evolution's interface is written in GTK3 (hundreds
of thousands of lines: windows, list views, editors,
preferences). GTK draws its widgets itself; they are not Win32
controls. Two levels are therefore possible:

### 6a — Native integration of the existing GTK interface (abandoned)

| Item | Means |
| --- | --- |
| Buttons, lists, tabs, scrollbars, checkboxes | GTK **`win32`** theme built into GTK3: drawing through the Windows theme engine (uxtheme), hence rendering identical to native controls |
| Title bar, borders, Snap, shadows | **native** Windows decorations instead of GNOME decorations drawn by GTK (`GTK_CSD=0`, no *header bar* on Windows) |
| Font | Segoe UI (system font), Windows sizes |
| Open/Save a file | `GtkFileChooserNative` → Windows `IFileDialog` dialog |
| Printing | Windows print dialog (GTK already uses `PrintDlgEx` on Windows) + WebView2 printing |
| Notifications | Windows notifications (toasts) via `GNotification` (GLib Windows backend) |
| Icons | Fluent / Segoe Fluent Icons icon set instead of Adwaita |
| Dark mode, accent color | reading Windows preferences (`AppsUseLightTheme` registry value, accent color) |
| Taskbar | unread message badge (`ITaskbarList3`), *jump list* (New message, New appointment) |
| Default client | `mailto:`, `.eml`, `.ics`, `.vcf` in "Default apps" (planned for the MSI) |

### 6b — Truly native interface (Windows controls)

Using the Windows controls themselves (WinUI 3 / Windows App SDK, or
Win32/ComCtl) requires **rewriting the entire interface** of Evolution: only
EDS (accounts, mail via Camel, calendar, contacts), WebView2 and the
non-graphical logic would be reused. It is a new mail client
built on the Evolution engine.

**User decision (2026-10-31): interface rewritten with real
Windows controls (6b).** The engine kept: EDS (accounts, sources,
address books, calendars, services), Camel (IMAP, SMTP, POP3, local, and
later Microsoft 365 via evolution-ews), the Credential Manager,
NSS/S-MIME. Evolution's GTK interface is no longer the final target.

Additional choices (2026-10-31):

- **Technology: WinUI 3 (Windows App SDK) in C++/WinRT**, with Visual
  Studio 2022. C++ directly calls the EDS/Camel C libraries
  in-process; native controls (`NavigationView`, `TreeView`, `ListView`,
  `CalendarView`…), Fluent, dark mode, embedded WebView2.
- **GTK Evolution = transitional version**: we only finish displaying
  messages in WebView2 (phase 3, step 2) to have a usable
  client during the rewrite, then the GTK version no longer evolves. Phase
  6a is abandoned.

## Phase 6b — Native WinUI 3 client (`winui/` folder)

### Architecture

```text
winui/
  core/   evo-core.dll  (C, GLib) : EDS + Camel, dedicated GLib thread, C API + JSON
  app/    EvolutionWin.exe (C++/WinRT, WinUI 3) : native interface, WebView2
```

- **`evo-core`** (C, built by CMake like EDS) owns a **GLib thread**
  that runs the default main context: the source registry
  (`ESourceRegistry`), a `CamelSession` and all EDS/Camel objects live only there.
  The interface sends it requests (accounts, folders,
  messages, content, calendar, contacts); responses come back through callbacks,
  serialized as **JSON** (a simple and stable boundary: no GLib type on the C++ side).
- **`EvolutionWin`** (C++/WinRT, Windows App SDK 1.7, unpackaged,
  self-contained: `WindowsAppSDKSelfContained`) receives the callbacks on the
  GLib thread and forwards them to the interface thread through the `DispatcherQueue`.
  JSON read with `Windows.Data.Json`.
- Interface: `NavigationView` (Mail, Calendar, Contacts, Settings);
  mail in three panes: `TreeView` of folders, `ListView` of messages,
  `WebView2` control for reading.

Versions: Microsoft.WindowsAppSDK 1.7.260224002 (last monolithic package
before the split of 1.8), Microsoft.Windows.CppWinRT 2.0.250303.1,
Microsoft.Windows.SDK.BuildTools 10.0.26100.9457, Microsoft.Web.WebView2,
Microsoft.Windows.ImplementationLibrary. Restored by `nuget.exe`
(`tools\tools\nuget.exe`); built by MSBuild. The C++ WinUI project
templates are not installed in Visual Studio: the project is
written by hand.

### First milestone — reached on 2026-10-31

Native window, accounts and folders from EDS/Camel, message list,
reading a message in WebView2.

Build (after `build-eds.bat`):

```bat
tools\build-winui-core.bat   :: evo-core.dll + evo-core-test.exe -> .\build\
tools\build-winui-app.bat    :: nuget restore + MSBuild -> .\build\EvolutionWin.exe
tools\run-winui.bat          :: launches .\build\EvolutionWin.exe with the prefix in PATH
```

Execution (development): add `<workspace>\gtk-build\gtk\x64\release\bin`
to the `PATH` (evo-core, EDS, GLib), then launch `.\build\EvolutionWin.exe`
(or use `tools\run-winui.bat`). `evo-core-test.exe` checks the engine
alone in the console (read-only: `EVO_CORE_READ_ONLY=1`, no message is
marked as read).

#### evo-core (`winui/core`)

- `EvoCoreSession` (subclass of `CamelSession`): each service is
  configured from its EDS source (`e_source_camel_configure_service`), the
  display name comes from it too; authentication with the password
  stored by EDS (`e_source_credentials_provider_lookup_sync`, hence the
  Windows Credential Manager), without any interface; empty receive filter
  (no rules yet); invalid certificates refused (dialog
  to come).
- Dedicated data folders (`…\evolution\evolution-windows\mail`): the
  GTK version can run at the same time without sharing its caches.
- IMAP accounts are "offline stores": they must be brought
  online (`camel_offline_store_set_online_sync`) before any operation,
  otherwise "You must work online".
- Ignored accounts: search folders (`vfolder`, Evolution-specific rules)
  and RSS feeds (Evolution plugin).
- Message rendering: HTML part preferred, `cid:` images embedded as
  `data:`, plain text converted (white background).

#### EvolutionWin (`winui/app`)

- Hand-written MSBuild project (the C++ WinUI templates are not
  installed): Windows App SDK 1.7 **unpackaged and self-contained**
  (`WindowsPackageType=None`, `WindowsAppSDKSelfContained=true`): no
  installation of the Windows App SDK runtime is needed.
- `CoreBridge.h`: evo-core callbacks → `DispatcherQueue` of the
  interface thread (shared lifetime token: no callback after closing).
- `FolderItem` (`[bindable]` Windows Runtime class): contents of the
  `TreeView` nodes displayed by a `DataTemplate` (putting interface elements
  in the nodes would display their type name and break recycling).
- WebView2 reader: scripts disabled, remote content blocked
  (`WebResourceRequested` → 403), links opened in the default
  browser, data folder `%LOCALAPPDATA%\EvolutionWin\WebView2`
  (`WEBVIEW2_USER_DATA_FOLDER`, since the executable's folder is not
  writable under Program Files), white background.
- External content: each message is served at a unique address
  `http://reader.evolution.invalid/message-<pid>-<n>` (response supplied by
  `WebResourceRequested`, no more 2 MB limit of `NavigateToString`);
  remote content authorization applies only to the `ContentLoading` of
  that specific navigation, so that a late request from the
  previous message can neither load content nor open the banner. Blocked
  remote requests (http, https, ws, wss, ftp: images, CSS, fonts,
  frames, tracking pixels) are counted and open the "External content
  blocked" banner (show for this message, trust the sender
  or the domain). Navigations not requested by the user
  (`<meta refresh>`) are cancelled. Preferences (`Settings.h`):
  `privacy.remote-content` = `never` | `trusted` (default) | `always`,
  `privacy.trusted-senders` and `privacy.trusted-domains` (JSON arrays in
  lowercase, exact match of the From address or its domain).

Validated with the user's real IMAP account: tree with
unread counters, message list (unread in bold), reading of
text and HTML messages.

### Second milestone — refresh, composing and sending (2026-10-31)

evo-core:

- **Tracked folders**: opened folders stay cached; their
  `CamelFolder::changed` signal (emitted in the engine thread) becomes the
  `folder-changed` event (unread, total, added/removed/changed).
- **Fetch**: immediately after startup, then every 5 minutes,
  or on demand (`evo_core_refresh`): connection, inbox
  always tracked (new mail), refresh of opened folders,
  then `folders-updated` (the interface re-reads the counters).
- **Sending** (`evo_core_send_message`): SMTP submission servers added to the
  session; identity (name, address, Reply-To) and submission server read from the
  account's EDS identity source; plain-text MIME message (UTF-8,
  quoted-printable), `In-Reply-To`/`References` for replies, Bcc outside the
  headers (transport recipients only); copy into the identity's Sent
  folder (`folder://…` URI), unless the server already
  saved it. Events `sending` / `sent`.
- `evo_core_get_message` also returns the plain text (to quote; HTML
  converted if needed), `reply_to`, `message_id`, `references`.

EvolutionWin:

- Unread counters updated live (`FolderItem` observable through
  `INotifyPropertyChanged`), list reloaded when messages arrive
  (selection and reader preserved).
- Send/Receive button (F5); New message (Ctrl+N).
- **Native compose window** (`ComposeWindow`): sending account, To, Cc,
  Bcc, Subject, text with Windows spell checker; Send
  (Ctrl+Enter); errors displayed in the window.
- Reply (Ctrl+R), Reply all (Ctrl+Shift+R, without one's own address),
  Forward (Ctrl+F, "TR:" prefix — the French forward prefix): `> ` quoting, thread
  preserved.
- Method names `GetMessage`/`SendMessage` avoided: Win32 macros.

**First user trial: "Password not found".** EDS
stores credentials per server; the one for the SMTP submission server
did not exist (Evolution only asks for it on first send, through a
dialog). Fix: if the submission server has no stored
password, evo-core reuses that of the receiving account whose identity
sends through that server, provided the user name is the same
("same settings as the receiving server" behavior of other
clients).

### Third milestone — message actions and search (2026-10-31)

evo-core:

- **Flags** (`evo_core_set_message_flags`): toggles `seen`
  (`CAMEL_MESSAGE_SEEN`) or `flagged` (`CAMEL_MESSAGE_FLAGGED`) on a message
  and synchronizes with the server (`camel_folder_synchronize_message_sync`).
- **Deletion** (`evo_core_delete_message`): looks up the account's real trash
  folder (`CAMEL_FOLDER_TYPE_TRASH`); if the message is not already there, moves it
  there (`camel_folder_transfer_messages_to_sync`) and purges the
  source folder, otherwise marks it `CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN`
  and synchronizes. Emits `folders-updated`.
- **Move** (`evo_core_move_message`): transfers a message to another
  folder (`camel_folder_transfer_messages_to_sync`), synchronizes source
  and destination and emits `folders-updated`.
- **Search** (`evo_core_search_messages`): case- and accent-insensitive search
  (`g_str_match_string` + `g_utf8_casefold`) on Subject, From, To and
  Cc in the local summaries, combined with Camel's full-text search on the
  message body (`camel_folder_search_sync` with `body-contains`).

EvolutionWin:

- Completed reading pane command bar: **Read / Unread** (`Ctrl+U`),
  **Flag** (follow-up flag), **Move** (hierarchical drop-down menu of the account's
  folders), **Delete** (`Del`).
- Message list: follow-up flag and attachment (paperclip)
  icons on each row; **context menu** (right click) with
  Read/Unread, Flag, Move to and Delete; automatic selection of the next
  message after deletion or move.
- **Search** box (`SearchBox`): instant in-memory filtering as you
  type (`TextChanged`) and full search in the folder (headers
  + message bodies via `evo-core`) on submit (`Enter` or magnifier).

### Fourth milestone — Windows notifications and attachments (2026-10-31)

evo-core:

- **New mail notifications** (`new-mail`): when synchronizing
  the inboxes (after the first initial synchronization at
  startup), `evo-core` detects the new UIDs that were added and are unread and emits a
  `new-mail` event containing `account`, `folder`, `count`, `from` and
  `subject`.
- **Attachment listing and download**: `evo_core_get_message`
  now returns an `attachments` array (`index`, `filename`,
  `mime_type`, `size`) for all attached MIME parts (or non-inline ones outside the
  main body). New function `evo_core_save_attachment` that decodes and
  writes the chosen attachment to disk via `g_file_set_contents`.
- **Sending attachments**: `evo_core_send_message` accepts an
  optional `attachments` array of local paths and builds a
  `multipart/mixed` message combining the body and each Base64-encoded file
  (`CamelMimePart` with file name and detected MIME type).

EvolutionWin:

- On `new-mail`: Windows system notification (toast via
  `AppNotificationBuilder` / `AppNotificationManager`) and reminder in the status
  bar.
- Reading pane: horizontal `AttachmentBar` listing each
  attachment with its size; clicking opens a native `FileSavePicker` (initialized
  with the window's `HWND`) to save the file.
- Compose window (`ComposeWindow`): **Attach** button (`FileOpenPicker`
  multi-file), token bar to view or remove
  attachments before sending.

### Fifth milestone — account management and authentication (2026-10-31)

evo-core:

- **Password prompt** (`auth-required`): when the Windows Credential
  Manager does not have the password of a source or the
  server rejects it, `evo-core` emits an `auth-required` event (`source_uid`,
  `name`, `user`, `rejected`) deduplicated per session.
- **Saving the password** (`evo_core_save_password`): permanently stores
  the secret in the Windows Credential Manager via
  `e_source_credentials_provider_store_sync` and re-arms the source for the
  next attempt.
- **Adding and removing accounts** (`evo_core_add_account`,
  `evo_core_remove_account`): atomic creation of the 3 EDS sources of a mail
  account (`ESourceMailAccount` + `CamelImapxSettings`,
  `ESourceMailIdentity` + `ESourceMailSubmission`, `ESourceMailTransport` +
  `CamelSmtpSettings`) via `e_source_registry_create_sources_sync`, password
  storage and immediate addition of the services to the `CamelSession`. Full
  removal of the 3 sources and disconnection of the corresponding services.

EvolutionWin:

- Automatic modal `ContentDialog` on the `auth-required` event allowing
  entry and saving of the password, then restarting the synchronization.
- **Settings / Mail accounts** view (`SettingsViewPane`): list of configured
  accounts (address, protocol, removal of non-local accounts)
  and **Add an IMAP / SMTP account** wizard (account name, full name,
  email address, IMAP/SMTP servers and ports, password).

### Sixth milestone — EDS calendar and contacts (2026-10-31)

evo-core (linked to `libecal-2.0` and `libebook-1.2`):

- **Calendar** (`evo_core_list_events`, `evo_core_create_event`,
  `evo_core_delete_event`): connection to `E_SOURCE_EXTENSION_CALENDAR` sources
  via `e_cal_client_connect_sync` (`E_CAL_CLIENT_SOURCE_TYPE_EVENTS`), query
  of `ICalComponent` components (`VEVENT`) and conversion of dates to
  Unix timestamps, creation and deletion of events.
- **Contacts** (`evo_core_list_contacts`, `evo_core_create_contact`,
  `evo_core_delete_contact`): connection to `E_SOURCE_EXTENSION_ADDRESS_BOOK` sources
  via `e_book_client_connect_sync`, filtered search (`EBookQuery`) on name
  and email address, reading of `EContact` fields (`full_name`, `email`,
  `phone`, `org`, `note`), adding and removing contacts.

EvolutionWin:

- **Calendar** view (`CalendarViewPane`): monthly `CalendarView` to filter
  by day (or display the next 30 days), event list with
  times, location, description, deletion and **New
  event** dialog (`CalendarDatePicker`, `TimePicker`).
- **Contacts** view (`ContactsViewPane`): instant search, contact
  list, detail card with **Write a message** actions (opens
  `ComposeWindow` pre-filled with the recipient) and **Delete contact**,
  as well as a **New contact** dialog.

### Seventh milestone — HTML editor (WebView2), native printing and repository reorganization (2026-10-31)

Repository reorganization (`Evolution-WindowsPort`):

- `WINDOWS-PORT.md`, `README.md` and `.gitignore` placed at the repository root.
- MSVC porting scripts and tools grouped in `./tools` (`build-*.bat`, `env.bat`, `check-prereqs.ps1`, `cmake/`, `compat/`, `patches/`, `pkgconfig/`, `webkit-shim/`, `tools/nuget.exe`).
- Native WinUI 3 client moved to the root in `./winui` (`winui/core` and `winui/app`).
- `./build` directory containing all ready-to-use final executables (`EvolutionWin.exe`, `evo-core.dll`, `evo-core-test.exe`, `evolution.exe`, `libexec/*.exe`) and `./build/intermediate` (ignored by Git) for CMake/MSBuild object files.
- GTK stack (`./gtk-build`) integrated at the project root and detected automatically by `tools\env.bat`.

evo-core:

- **Sending HTML messages (`multipart/alternative`)**: `evo_core_send_message` now accepts an optional `"html"` field in addition to `"text"`. When `"html"` is provided, the engine builds a `multipart/alternative` container combining the `text/plain; charset=utf-8` part (generated automatically via `html_to_text` if `"text"` is empty) and the `text/html; charset=utf-8` part (both encoded as `quoted-printable`). If attachments (`"attachments"`) are present, the `multipart/alternative` is the first part of the `multipart/mixed` container.

EvolutionWin:

- **Rich HTML editor on WebView2** (`ComposeWindow`):
  - `contenteditable="true"` editing area powered by WebView2 (`HtmlEditor`) with spell checker enabled, Segoe UI font and forwarding of the `Ctrl+Enter` shortcut to message sending via `WebMessageReceived`.
  - Formatting toolbar (`FormatToolbar`): **Bold** (`Ctrl+B`), **Italic** (`Ctrl+I`), **Underline** (`Ctrl+U`), **Strikethrough**, **Bulleted list**, **Numbered list**, **Block quote** (`blockquote`), **Insert hyperlink** (`ContentDialog` dialog asking for the URL) and **Clear formatting**.
  - **HTML format** toggle button in the command bar allowing switching at any time from rich HTML mode to Plain text mode (`TextBox`) with automatic conversion of the entered content.
  - Structured HTML quotes (styled `<blockquote>` for replies, formatted header for forwards) prepared by `MainWindow::Reply`.
- **Native message printing** (`MainWindow`): **Print** button (`Ctrl+P`) in the reading pane command bar opening WebView2's native print interface (`CoreWebView2::ShowPrintUI`).

### Eighth milestone — TLS certificates, OAuth2, S/MIME, OpenPGP, multiple selection (2026-10-31)

evo-core:

- **TLS certificates**: `EvoCoreSession::trust_prompt` emits `tls-trust-required`
  (host, SHA-256 fingerprint, issuer, validity, anomalies described in French)
  and waits for the answer (`evo_core_respond_tls_trust`: reject, accept for the
  session, always trust). Permanently approved certificates are saved in `…\evolution-windows\mail\trusted-certs.ini`.
- **OAuth2 (XOAUTH2) for IMAP/SMTP**: `get_oauth2_access_token_sync` reads the
  token from the Credential Manager (JSON `access_token`,
  `refresh_token`, `expires_at`, `client_id`, `token_endpoint`) and
  renews it with the `refresh_token` (libsoup) one minute before expiry.
  `evo_core_add_account` accepts `auth_mechanism` (`XOAUTH2`).
- **S/MIME and OpenPGP on reading**: decryption (`multipart/encrypted`,
  `application/pkcs7-mime`) then signature verification (`multipart/signed`),
  result in the message's `security` object.
- **S/MIME and OpenPGP on sending**: options `pgp_sign`, `pgp_encrypt`,
  `smime_sign`, `smime_encrypt` (and `smime_key_id`) of `evo_core_send_message`.
- **`.eml` export** (`evo_core_export_messages_eml`): messages written as
  RFC 822 in a temporary folder, for forwarding as attachments.

EDS (`camel-gpg-context.c`, phase 4c):

- `gpg.exe` searched in the PATH then in the GnuPG /
  Gpg4win installation folders; launched by `g_spawn_async_with_pipes()` (helper
  `gspawn-win64-helper.exe`), status lines on `--status-fd=2`, mixed with
  gpg's diagnostics and sorted out on parsing (CRLF removed).
- Outputs read without blocking (`PeekNamedPipe`). Standard input is written by
  a **dedicated thread**: Windows anonymous pipes have no non-blocking
  write, and writing from the loop would cause a deadlock as soon as
  gpg fills its output (message of a few KB).
- Passphrase: gpg runs with `--batch`, gpg-agent obtains it through
  its pinentry window (the Gpg4win one).
- **Test** (2026-10-31, `gpg` 2.4.9 from Git for Windows, test key):
  signing, verification ("good signature"), encryption and decryption
  of a 1 MB message in 0.1 to 0.3 s; identical content (Camel puts the text
  in canonical CRLF form before signing/encryption, as on Linux).

EvolutionWin:

- **TLS certificate** dialog (`PromptTlsCertificateAsync`): certificate
  details, Reject / Accept for this session / Always trust.
- Add-account wizard: Microsoft 365 or Google **OAuth2** authentication
  (PKCE S256, sign-in page in a WebView2, redirect
  `http://localhost/oauth2callback`). Application ID and tenant
  pre-filled by `EVO_OAUTH2_CLIENT_ID` / `EVO_OAUTH2_TENANT_ID`.
- Reading pane **security banner** (valid signature or not,
  encrypted message); **Sign / Encrypt (PGP)** and **Sign / Encrypt
  (S/MIME)** buttons in the compose window.
- **Multiple selection** in the list: read/unread, flag, move,
  delete, reply and forward of several messages (forwarding as
  `.eml` attachments).

Problems encountered:

1. **`.bat` scripts unreadable by `cmd.exe`** (`'ot' is not recognized…`):
   files with LF line endings. `.gitattributes` now enforces CRLF on
   `.bat`/`.cmd` (and LF on bash scripts), regardless of `core.autocrlf`.
2. **`co_await` in a `catch` block** (C2304, OAuth2 token exchange):
   error message stored, displayed after the block.
3. **No account at startup** ("No provider available for protocol imapx"):
   an old copy of `camel-1.2.dll` in `build\` was loaded first; Camel computes its paths from
   the location of its DLL and
   was looking for its providers in `build\lib\…`. **Do not copy any EDS DLL into
   `build\`**: they must be loaded from the prefix (PATH, as
   `run-winui.bat` does) until the installer (phase 5) has built
   a complete tree. An old `evolution-source-registry` launched from
   `C:\gtk-build` (before the reorganization) was also occupying the D-Bus bus.

4. **Empty inbox on first launch** (fresh Windows 11 VM,
   MSI 0.9.0; other folders correct): the startup fetch opened the
   inbox through `camel_store_get_inbox_folder_sync`, whose
   default (IMAP) implementation creates a **second `CamelFolder` outside the
   store's folder cache**. evo-core tracked that instance; the
   messages downloaded by the IMAP server went into the other one (the one in the
   cache): the list stayed empty, with no error. With an existing cache, the
   two instances re-read the same summary on disk, hence a problem
   invisible on the development machine. Fix: inbox name
   read from the tree (`CAMEL_FOLDER_TYPE_INBOX`), folder
   opened like the others (`camel_store_get_folder_sync`). In addition,
   `core_ensure_connected` serializes connections and checks the
   `CONNECTED` state: `camel_offline_store_set_online_sync` answers "success"
   immediately if another thread is connecting, and an IMAP fetch
   started then is ignored without error. Reproduced with an empty cache
   (`XDG_DATA_HOME` / `XDG_CACHE_HOME` variables) and a second list
   triggered by `folder-changed`, as the interface does: 3 failures
   out of 10 before, 0 out of 15 after (MSI 0.9.1).

**Validated**: `evo-core-test` (read-only) with the real IMAP account:
accounts, folders, 56 messages, reading a message. Still to validate in the
interface: TLS dialog, OAuth2 sign-in (Entra ID or Google
application registration required), S/MIME (personal certificate in NSS) and
OpenPGP with Gpg4win.

### Next steps for the native client

1. ~~Automatic refresh~~; ~~composing and sending~~ (plain text, HTML `multipart/alternative` and attachments).
2. ~~Actions: delete, move, mark read/unread, flag; search.~~
3. ~~Windows notifications (toasts) for new mail and attachments.~~
4. ~~Account management (IMAP/SMTP add wizard, password) and settings.~~
   ~~Configurable directly in the application.~~
5. ~~Calendar and contacts (EDS): `CalendarView`, contact lists and cards.~~
6. ~~HTML compose editor (WebView2) and native message printing (`Ctrl+P`).~~
7. ~~TLS certificate dialog, IMAP/SMTP OAuth2, S/MIME, OpenPGP~~ (validation
   in the interface still to do).
8. ~~MSI installer (phase 5).~~
9. Microsoft 365 through Microsoft Graph (evolution-ews, phase 4b).

## Phase tracking

- [x] Phase 0 — MSVC GTK3 stack (completed on 2026-10-31, ~45 min; gvsbuild
  also built gtk4, required by adwaita-icon-theme)
- [x] NSPR / NSS built with MSVC
- [x] libical / libical-glib built with MSVC
- [x] Phase 1 — EDS built and source registry started (address book /
  calendar factories: validated with EvolutionWin)
- [x] Phase 2 — Evolution built (empty WebKit shim) and launched: working main
  window (2026-10-31)
- [x] Phase 2b — automatic start of the EDS services; real IMAP account
  synchronized (SMTP sending validated)
- [x] Phase 3 — EWebView on WebView2 (mail reading): done for the
  transitional GTK version (2026-10-31)
- [x] Phase 4 (WinUI 3) — WebView2 HTML editor and native printing done in `EvolutionWin` (S/MIME to come)
- [~] Phase 4b — evolution-ews built (MSVC), Microsoft 365 accounts (Graph,
  OAuth2 WebView2) in EvolutionWin; test with a real tenant to do
- [x] Phase 4c — OpenPGP: `camel-gpg-context.c` ported to
  `g_spawn_async_with_pipes()` (tested on 2026-10-31); still to integrate
  Gpg4win as an optional prerequisite of the MSI installer
- [x] Phase 5 — standalone MSI installer (WiX v5), validated by extraction
  (2026-10-31); icon, associations and `mailto:` to come
- [—] Phase 6a — abandoned (the GTK version stays transitional)
- [x] Phase 6b — **chosen**: interface rewritten in WinUI 3 / C++/WinRT (milestones 1 to 7 done: mail, WebView2 reading, HTML/text composing and sending, printing, actions, search, toast notifications, attachments, account and password management, EDS calendar and contacts)
