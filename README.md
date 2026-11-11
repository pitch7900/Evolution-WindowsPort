<!-- cspell:disable -->
# Evolution for Windows 11 (Native MSVC & WinUI 3 Port)

**English** | [Français](./README.fr.md)

This repository contains the **100% native Windows 11 port** of the open-source **GNOME Evolution** mail, calendar and address book client, built with **Microsoft Visual C++ (MSVC)** (no MinGW, no WSL and no Cygwin) and featuring a **native WinUI 3 user interface (`EvolutionWin`)**.

**Download:** ready-to-use MSI installers (Windows 11 x64) are published in the [**GitHub Releases**](https://github.com/pitch7900/Evolution-WindowsPort/releases) — one Release per version, with the `Evolution-<version>-x64.msi` installer, its SHA-256 checksum (`.sha256`) and the notes from the [CHANGELOG](./CHANGELOG.md). [**Latest version**](https://github.com/pitch7900/Evolution-WindowsPort/releases/latest).

For the detailed technical log, the history of CMake/MSVC fixes and phase tracking, see [**WINDOWS-PORT.md**](./WINDOWS-PORT.md).

---

## Upstream GNOME Projects

| Project | GitHub repository (official mirror) | Development repository | Base version |
| --- | --- | --- | --- |
| Evolution | [github.com/GNOME/evolution](https://github.com/GNOME/evolution) | [gitlab.gnome.org/GNOME/evolution](https://gitlab.gnome.org/GNOME/evolution) | commit `be33fb6204` |
| Evolution Data Server | [github.com/GNOME/evolution-data-server](https://github.com/GNOME/evolution-data-server) | [gitlab.gnome.org/GNOME/evolution-data-server](https://gitlab.gnome.org/GNOME/evolution-data-server) | commit `ea6b0124d` |
| evolution-ews (Exchange / Microsoft 365) | [github.com/GNOME/evolution-ews](https://github.com/GNOME/evolution-ews) | [gitlab.gnome.org/GNOME/evolution-ews](https://gitlab.gnome.org/GNOME/evolution-ews) | commit `19cb82b3` |

---

## Project Architecture

The project is built around two main layers:

1. **Mail, calendar and contacts engine (`evolution-data-server`, `Camel` and `evo-core.dll`)**:
   - Native MSVC (x64) build of **Evolution Data Server (EDS)** and **Camel** (IMAP, POP3, SMTP, local folders, S/MIME via NSS).
   - Native integration with the **Windows Credential Manager** (`CredWriteW` / `CredReadW` / `CredDeleteW`, DPAPI protection) replacing `libsecret`.
   - Automatic, console-window-free startup of the EDS D-Bus services (`evolution-source-registry`, address book and calendar factories).
   - Bridge library **`evo-core.dll`** (`winui/core/`) exposing an asynchronous C API with JSON serialization to the graphical interface.

2. **Native Windows 11 interface (`EvolutionWin.exe` — WinUI 3 / C++/WinRT)**:
   - Native Windows App SDK controls (`NavigationView`, `TreeView`, `ListView`, `CalendarView`, `AutoSuggestBox`, `ContentDialog`) with **Mica** rendering and system theme support (light/dark).
   - **HTML reading and composing** powered by **Microsoft Edge WebView2** (rich-text formatting toolbar, HTML / plain text toggle, remote content blocking in the reader).
   - **Native printing** of messages via WebView2 (`ShowPrintUI`).
   - **Windows system notifications (Toasts)** on new message arrival, **attachment** management, instant and full-text **search**, EDS **calendar** and **contacts**.
   - A transitional GTK3 version (`evolution.exe` + a `webkit2gtk-4.1.dll` shim on top of WebView2) is also available.

---

## Main Available Features

EvolutionWin brings the full set of proven capabilities of the GNOME Evolution engine into a modern, fluid user experience designed specifically for Windows 11:

### ✉️ Mail & Messaging
- **Multiple accounts and standard protocols**: full support for the IMAP, POP3 and SMTP protocols and for Microsoft 365 / Exchange Online accounts via Microsoft Graph (`evolution-ews`).
- **Modern and secure display**: HTML rendering powered by Microsoft Edge WebView2, with automatic blocking of remote images and content to protect privacy, and an interactive unblock banner.
- **Rich composer**: composing in HTML or plain text, rich formatting, default font and size selection, native Windows spell checking, read receipts (Disposition-Notification-To) and automatic hidden copy (Bcc).
- **Smart quoting**: multilingual detection and customization of reply and forward prefixes (Re:, TR:, Fwd:, etc.) with contextual quoting according to the interface language.
- **Signature manager**: multiple signatures with per-account assignment and placement above or below the quoted text.
- **Attachments & built-in preview**: quick access bar with icons by MIME type, built-in preview for images and text documents, one-click save and open.
- **Export and interoperability**:
  - **Export to the standard .eml format**: single or batch save with the native Windows file picker (Ctrl+S).
  - **Drag & drop to Windows**: drag one or more emails directly to Windows Explorer or the Desktop.
- **Visual categories**: quick assignment of colored Fluent categories (10 standard hues with visual badges in the list) synchronized with Camel flags.
- **Advanced tools**: full RFC 822 raw stream / MIME source viewer with instant search (Ctrl+Shift+U), native text-to-speech for audio reading of emails, and native printing via WebView2.

### 📅 Calendar & Agendas
- **Proven Evolution Data Server engine**: high-performance storage and local and remote synchronization of events.
- **Multiple time views**: dynamic switching between Day, Work Week, Full Week and Month views.
- **Side mini-calendar**: monthly date picker in the side pane with a ribbon command and a dedicated show/hide option.
- **Dual time zone management**: simultaneous display of two configurable time zones for coordinating remote teams.
- **Reminders & Alerts**: due-date notification system that can be enabled or disabled globally.

### 👥 Address Book & Contacts
- **Multiple address books**: centralized management of local and corporate contacts.
- **Smart autocomplete**: instant recipient suggestions in the address fields (To, Cc, Bcc).
- **Complete contact cards**: view and edit contact details, addresses, phone numbers and professional information.

### 📋 Tasks & Memos
- **Activity tracking**: task management with statuses, priorities and due dates.
- **To-Do Bar side pane**: view and quickly manage urgent tasks without leaving the mail view.

### 💻 Windows 11 Experience & System Integration
- **Fluent Design & Mica effect**: modern interface with translucent Mica materials, rounded corners and Segoe UI Variable typography.
- **System theme and customization**: full support for Light and Dark modes (automatic following Windows or forced) and forced selection of the interface language.
- **Taskbar notification badge**: unread message counter rendered in high-definition 32-bit ARGB with perfect alpha transparency and automatic DPI scaling.
- **Windows system notifications (Toasts)**: new-mail alerts customizable globally, per account or per folder, with a switchable sound signal.
- **DPAPI security and encryption**: protected storage of all credentials and passwords in the Windows Credential Manager.
- **Automatic startup**: option to launch at Windows startup via the user registry (HKCU\Software\Microsoft\\Windows\\CurrentVersion\\Run).
- **Help and keyboard shortcuts (F1)**: interactive keyboard shortcut documentation center with instant search and switchable profiles (Outlook vs GNOME Evolution).
- **Network and health monitoring**: status bar with a dynamic indicator and detection of synchronization anomalies.
- **Easier maintenance**: option to automatically empty the Trash when the application closes.

---

## Repository Layout

| Directory / File | Role |
| --- | --- |
| [`WINDOWS-PORT.md`](./WINDOWS-PORT.md) | Full technical documentation and progress tracking of the port |
| [`CHANGELOG.md`](./CHANGELOG.md), [`VERSION`](./VERSION) | Changes per version; current version (used by the installer) |
| [`TODO.md`](./TODO.md) | Features still to be implemented |
| [`winui/`](./winui) | Native Windows 11 client: `winui/core` (`evo-core.dll`, one source file per domain: session, accounts, folders, messages, calendar, tasks, contacts...) and `winui/app` (`EvolutionWin.exe`) |
| [`tools/`](./tools) | MSVC build scripts (`build-*.bat`, `env.bat`, `check-prereqs.ps1`), CMake layer (`cmake/msvc-compat.cmake`), POSIX compatibility headers (`compat/`), WebKit2 shim (`webkit-shim/`) and utilities (`tools/nuget.exe`) |
| `build/` | Build outputs (`EvolutionWin.exe`, `evo-core.dll`, `evolution.exe`, `libexec/*.exe`, `build/intermediate/`), ignored by Git |
| `dist/` | MSI installers built locally by `build-msi.bat`, ignored by Git (published versions are in the [Releases](https://github.com/pitch7900/Evolution-WindowsPort/releases)) |
| [`evolution/`](./evolution) | Evolution source code adapted for MSVC |
| [`evolution-data-server/`](./evolution-data-server) | Evolution Data Server source code adapted for Windows 11 / MSVC |
| [`evolution-ews/`](./evolution-ews) | evolution-ews source code (Microsoft 365 via Microsoft Graph, Exchange Web Services) built with MSVC |
| [`deps/`](./deps) | Automatically downloaded third-party sources (NSS, NSPR, libical, WebView2 SDK) |
| [`gtk-build/`](./gtk-build) | GTK3 / GLib / libsoup3 stack built with `gvsbuild` and a shared install prefix (`gtk-build/gtk/x64/release`, ignored by Git) |

---

## Quick Start

### 1. Check and install prerequisites

From a command prompt at the repository root:

```bat
tools\check-prereqs.bat            :: Checks the environment and prints a report
tools\check-prereqs.bat -Install   :: Automatically installs/downloads whatever is missing
```

### 2. Build the project (in order)

```bat
tools\build-gtk.bat          :: GTK3 / GLib stack via gvsbuild (1 to 2 h the first time)
tools\build-nss.bat          :: NSPR + NSS (S/MIME and TLS)
tools\build-libical.bat      :: libical + libical-glib (built-in time zones)
tools\build-eds.bat          :: evolution-data-server
tools\build-ews.bat          :: evolution-ews (Microsoft 365 / Exchange Online)
tools\build-webkit-shim.bat  :: WebKitGTK 4.1 shim on WebView2 (for the GTK version)
tools\build-evolution.bat    :: Evolution (transitional GTK version)
tools\build-winui-core.bat   :: evo-core.dll (C engine for the WinUI 3 client)
tools\build-winui-app.bat    :: EvolutionWin.exe (native WinUI 3 client, generated in .\build)
```

### 3. Launch the native Windows 11 client

```bat
tools\run-winui.bat          :: Launches .\build\EvolutionWin.exe with the prefix in the PATH
tools\run-winui-debug.bat    :: Launches EvolutionWin.exe with IMAP/evo-core debug logs
```

### 4. Build the MSI installer

```bat
tools\build-msi.bat          :: dist\Evolution-<version>-x64.msi (WiX v5, standalone)
msiexec /i dist\Evolution-<version>-x64.msi /qn   :: silent installation (per machine)
```

### 5. Publish a release

```bat
tools\publish-release.bat -DryRun   :: checks and notes, publishes nothing
tools\publish-release.bat           :: vX.Y.Z tag + GitHub Release (MSI and .sha256 attached)
```

The script checks that the MSI exists, that the [CHANGELOG](./CHANGELOG.md) section is dated and that the Release does not exist yet; `-PreRelease` (or an `x.y.z-suffix` version) publishes a pre-release. GitHub token: `GH_TOKEN`, otherwise Git credentials.

---

## License

This project is distributed under the **GNU Lesser General Public License, version 2.1 or (at your option) any later version** (`LGPL-2.1-or-later`), the text of which is in [`LICENSE`](./LICENSE). This is the license of the original GNOME projects:

- **Evolution**: LGPL v2 or (at your option) LGPL v3, see [`evolution/COPYING`](./evolution/COPYING). A few files fall under other licenses (OpenLDAP, MPL/GPL/LGPL), which are listed there.
- **Evolution Data Server**: LGPL v2 or later, see [`evolution-data-server/COPYING`](./evolution-data-server/COPYING).
- **evolution-ews**: LGPL v2.1 or later, see [`evolution-ews/COPYING`](./evolution-ews/COPYING).
- **SQLite**: public domain (no restrictions or attribution requirements), see [sqlite.org/copyright.html](https://www.sqlite.org/copyright.html). Evolution Data Server and Camel already use it for their caches (message index, address books, calendars); the `sqlite3.dll` library is shipped with the application.

Files taken from these projects retain their original license and copyright notices (`SPDX-License-Identifier` headers).
