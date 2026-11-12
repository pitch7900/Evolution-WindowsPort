# AI Agent Reference Guide — EvolutionWin (Evolution Windows Port)

This document is the complete reference manual for any AI agent or developer taking over the development of **EvolutionWin**. It summarizes the architecture, the environment, the contribution rules, the build and test commands, and the project's progress status.

---

## 1. Overview & Project Identity

**EvolutionWin** is a native, modern port to **Windows 11** of the Evolution collaborative suite (GNOME's mail client, Calendar, Contacts and Task manager).

- **Underlying engine**: `evolution-data-server` (EDS), Camel (IMAP, SMTP, POP3, EWS protocol stack), GLib/GObject, libxml2, libsoup-3.0, libical.
- **Native bridge (C)**: `winui/core/evo-core.dll` wraps the GLib/EDS types and event loops to expose a clean C API based on JSON exchanges and asynchronous callbacks.
- **User interface**: **WinUI 3** (Windows App SDK 1.7) in C++/WinRT, faithfully following the Windows 11 visual style (Fluent Design, Mica, dark/light mode, modern controls).
- **User & Owner**: **Pierre** (address him informally, communicate in French).

---

## 2. Development Environment & Prerequisites

- **Operating system**: Windows 11 x64 (22H2 or later).
  > **Mandatory note**: This is a Windows 11 machine. Never run Linux/Bash-style commands (use PowerShell `pwsh` or `.bat` scripts).
- **Compiler & Tools**:
  - Visual Studio 2026 Community (MSVC v180, C++20 / C++latest).
  - CMake 3.25+ & Ninja.
  - Windows App SDK 1.7 (native NuGet packages embedded in `winui/packages/`).
  - WiX Toolset v3.14 (for generating MSI installers).
  - Python 3.10+ (for maintenance and test scripts).

---

## 3. Repository Layout

```
c:\Users\chris\VSCode\Evolution-WindowsPort\
├── VERSION                       # Current SemVer version number (e.g. 0.9.8)
├── ROADMAP.md                    # Roadmap by version milestones (v0.9.8, v1.0.0...)
├── CHANGELOG.md                  # Change history (Keep a Changelog)
├── TODO.md                       # Exhaustive reference of specifications and tasks
├── QUICKWINS.md                  # High-impact priority tasks (evolving)
├── TESTS.md                      # Guide and specification of the test suite
├── locks/                        # Local JSON locks against multi-agent collisions (git-ignored)
│
├── winui/
│   ├── core/                     # C engine (evo-core.dll), split by functional domain
│   │   ├── evo-core.h            # Public API (declarations and ABI exports)
│   │   ├── evo-core-private.h    # Private header: includes, struct EvoCore, shared types and prototypes
│   │   ├── evo-core.c            # Lifecycle: log, EDS modules, GLib thread, new/free, offline mode
│   │   ├── evo-core-session.c    # CamelSession: authentication, TLS certificates
│   │   ├── evo-core-events.c     # JSON builders and event emission
│   │   ├── evo-core-requests.c   # Asynchronous request queue (core_submit), stores, connection
│   │   ├── evo-core-accounts.c   # List, default account, deletion
│   │   ├── evo-core-account-setup.c  # IMAP/POP3/SMTP add, test, read, edit, passwords
│   │   ├── evo-core-account-cloud.c  # Microsoft 365 / Exchange (EWS) and Google accounts
│   │   ├── evo-core-detect.c     # Automatic detection (ISPDB, DNS, Autodiscover)
│   │   ├── evo-core-oauth2.c     # OAuth2 sign-in and credential requests
│   │   ├── evo-core-folders.c    # Tree, roles, tracking of open folders
│   │   ├── evo-core-folder-ops.c # Create, rename, move, copy, delete, quota
│   │   ├── evo-core-messages.c   # List of a folder's messages
│   │   ├── evo-core-search.c     # Search (folder, subfolders, account, all) and unified multi-account views
│   │   ├── evo-core-message-read.c    # Reading, attachments, .eml export
│   │   ├── evo-core-message-actions.c # Flags, follow-up, move, copy, delete
│   │   ├── evo-core-refresh.c    # Polling, periodic synchronization, reconnection
│   │   ├── evo-core-send.c       # MIME sending and Outbox
│   │   ├── evo-core-drafts.c     # Drafts
│   │   ├── evo-core-signatures.c # Signatures
│   │   ├── evo-core-calendar.c   # Calendar events
│   │   ├── evo-core-tasks.c      # Tasks (VTODO)
│   │   ├── evo-core-contacts.c   # Contacts and recipient autocompletion
│   │   ├── evo-core-sources.c    # EDS sources and calendar creation
│   │   ├── evo-core-export.c     # vCard / iCalendar export
│   │   └── CMakeLists.txt        # Engine CMake configuration
│   └── app/                      # WinUI 3 interface (C++/WinRT)
│       ├── App.xaml / App.xaml.cpp
│       ├── MainWindow.xaml      # GENERATED (git-ignored) by tools\assemble-xaml.ps1: never edit it
│       ├── MainWindow.xaml.h    # Main window and overview
│       ├── xaml/                # Sources of the main window XAML:
│       │   ├── MainWindow.xaml.in            # Template (shell: window, navigation, areas) with <!--#include xaml/X.xamlpart--> lines
│       │   ├── TitleBar / NavigationMenuItems / StatusBar / TodoBar .xamlpart
│       │   ├── Ribbon.Mail / .Calendar / .Contacts / .Tasks .xamlpart    # Ribbons per module
│       │   ├── Mail.Folders / .Splitters / .MessageList / .Reader .xamlpart
│       │   ├── CalendarView / ContactsView / TasksView .xamlpart
│       │   └── Settings.Navigation + Settings.<Category>[.<Section>] .xamlpart   # One file per Settings category or section
│       ├── MainWindow.*.cpp      # Modular split of the code-behind:
│       │   ├── MainWindow.Accounts.cpp       # Accounts, synchronization, quota
│       │   ├── MainWindow.Attachments.cpp    # Attachments, preview, EML export
│       │   ├── MainWindow.Calendar.cpp       # Day/Week/Month grids, filters
│       │   ├── MainWindow.Compose.cpp        # Compose-opening commands
│       │   ├── MainWindow.Contacts.cpp       # Address books and cards
│       │   ├── MainWindow.Events.cpp         # Calendar events and reminders
│       │   ├── MainWindow.Folders.cpp        # Folder tree and actions
│       │   ├── MainWindow.Layout.cpp         # Panes, splitters, layout
│       │   ├── MainWindow.MessageActions.cpp # Reply, forward, archive, junk
│       │   ├── MainWindow.MessageFilter.cpp  # Reading filters, quick search
│       │   ├── MainWindow.MessageList.cpp    # Message grid, conversations
│       │   ├── MainWindow.Reader.cpp         # WebView2 reading pane, zoom
│       │   ├── MainWindow.Ribbon.cpp         # Fluent ribbon and contextual tabs
│       │   ├── MainWindow.Settings.cpp       # Application settings panel
│       │   ├── MainWindow.Signatures.cpp     # HTML/text signature manager
│       │   ├── MainWindow.Sources.cpp        # EDS calendar/contact sources
│       │   ├── MainWindow.StatusBar.cpp      # Status bar, progress, cancellation
│       │   └── MainWindow.Tasks.cpp          # Task lists and memos
│       ├── MainWindow.Helpers.h  # Standalone algorithms, MIME parsing, localization
│       ├── CoreBridge.h          # C++/WinRT gateway wrapping evo-core.dll
│       ├── Settings.h            # JSON persistence of settings and preferences
│       ├── UnifiedViews.h        # Unified multi-account views: pure functions (composite uid, included accounts)
│       ├── ComposeWindow.xaml / .cpp / .h # Email compose window
│       ├── MessageWindow.xaml / .cpp / .h # Standalone message viewing window
│       └── EvolutionWin.vcxproj  # MSBuild project for the WinUI application
│
├── tests/
│   ├── unit/
│   │   ├── test_helpers.cpp             # WinUI & Helpers unit tests (54 tests)
│   │   ├── test_evo_core_contracts.c    # JSON contract & C robustness tests (12 tests)
│   │   └── test_settings.cpp            # Preferences persistence tests (5 tests)
│   └── abi/
│       └── test_abi_exports.ps1         # Dynamic validation of the evo-core.dll ABI (67 exports)
│
├── tools/
│   ├── build-winui-core.bat      # Build evo-core.dll (CMake/Ninja)
│   ├── build-winui-app.bat       # Build EvolutionWin.exe (MSBuild)
│   ├── run-tests.bat             # Full run of the test suite (100% SUCCESS required)
│   ├── build-msi.ps1             # Generate the MSI installation package (WiX)
│   ├── publish-release.ps1       # Publish MSI artifacts to GitHub Releases
│   └── run-winui.bat             # Quick launch of the built application
│
└── gtk-build/ / evolution-data-server/ # Supporting binaries and underlying sources
```

---

## 4. Essential Commands (Development Cycle)

Each command must be run from the repository root:

### 4.1. Build the C engine (`evo-core.dll`)
```powershell
.\tools\build-winui-core.bat
```
- Produces `build\evo-core.dll` and `build\evo-core.lib` via CMake and Ninja in a few seconds.

### 4.2. Build the WinUI application (`EvolutionWin.exe`)
```powershell
.\tools\build-winui-app.bat
```
- Builds the WinUI 3 application in Release x64 via `MSBuild`.

### 4.3. Run the entire unit test suite
```powershell
.\tools\run-tests.bat
```
- Builds and successively runs:
  1. `test_helpers.exe` (54 algorithmic tests).
  2. `test_evo_core_contracts.exe` (12 C robustness and JSON contract tests).
  3. `test_settings.exe` (5 serialization and resilience tests).
  4. `test_abi_exports.ps1` (strict check of the functions exported by the DLL).
- **Absolute rule**: 100% of the tests must be green before any commit.

### 4.4. Launch the application
```powershell
.\tools\run-winui.bat
```

### 4.5. Build the installation MSI
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-msi.ps1
```
- Produces `build\Evolution-Setup-<version>.msi`.

---

## 5. Strict Development & Contribution Rules

The following rules were defined by Pierre and must be followed to the letter:

1. **French language mandatory**:
   - All exchanges, documentation, graphical interfaces, commit messages and code comments must be in **French**.
   - Code must be systematically commented (doxygen `@brief`, `@param`, `@return`).
   - Remember to unescape apostrophes in console output if necessary.

2. **Git & Commit strategy**:
   - Work on the active branch of the current version (currently `0.9.8`).
   - Make **one atomic commit after each completed task**.
   - Conventional commit format: `feat(<scope>): <description in French>` or `fix(<scope>): <description>`.
   - Always push (`git push origin <branch>`) after committing a set of tasks.

3. **Continuous documentation updates**:
   - As soon as a task is completed, tick its box in `TODO.md` (`- [x]`).
   - Add an explanatory paragraph under the `## [<version>] — en cours` section in `CHANGELOG.md`.

---

## 6. Architecture & Design Patterns

### 6.1. The `evo-core` (C) <-> `CoreBridge` (C++/WinRT) Bridge
- All I/O operations (network, disk, IMAP/SMTP protocols) run on background threads via `core_submit()` (`evo-core-requests.c`).
- The engine is split into one file per domain (`evo-core-<domain>.c`): each public `evo_core_xxx` function is defined next to its executor `core_xxx_run`. Internal functions shared between files (prefix `core_`) are declared in `evo-core-private.h`; any new shared function is added there, and any new `.c` file is added to `winui/core/CMakeLists.txt`. `evo-core.h` remains the only public interface.
- Communication relies on **JSON**:
  - Input: serialized JSON string or typed C parameters.
  - Output: `EvoCoreResultFunc` callback of type `void (const char *json_result, const char *error_message, void *user_data)`.
- `CoreBridge` (`winui/app/CoreBridge.h`) receives the result and automatically re-routes execution onto the **WinUI UI thread** via `DispatcherQueue::TryEnqueue()`.

### 6.2. Preferences and Settings (`Settings.h`)
- Settings are stored in JSON format in `%LOCALAPPDATA%\EvolutionWin\settings.json`.
- Singleton access via `EvolutionWin::Settings::Instance()`.
- Typed methods: `GetBoolean()`, `SetBoolean()`, `GetString()`, `SetString()`, `GetInt()`, `SetInt()`, `GetArray()`, etc.
- Always provide a default fallback value when reading.

### 6.2 bis. Main window XAML (fragments)
- `winui/app/MainWindow.xaml` exceeded 5,800 lines: it is **generated** at each build (MSBuild target `AssembleMainWindowXaml`, `InitialTargets` of `EvolutionWin.vcxproj`) by `tools\assemble-xaml.ps1`, from the template `winui/app/xaml/MainWindow.xaml.in` and the fragments `winui/app/xaml/*.xamlpart`. The generated file is ignored by git.
- **Always edit the template or a fragment, never `MainWindow.xaml`** (it would be overwritten). A `<!--#include xaml/Name.xamlpart-->` line in the template is replaced verbatim by the fragment's content; a fragment is a well-formed XML subtree (one or more sibling elements), with no XML header or namespace declarations (`local:`, `x:`, `controls:` come from the window).
- To add a section: create `xaml/<Name>.xamlpart`, put the element in it, add the include line at the right place in the template. No C++ code change: all named elements (`x:Name`) remain members of `MainWindow` (this is the reason for this split rather than `UserControl`s).
- XAML compiler errors cite `MainWindow.xaml` (generated): open that file at the indicated line to locate the faulty element, then fix it in the corresponding fragment.

### 6.3. Localization, Translations and Unicode on Windows MSVC
- **Beware of Unicode case folding (accents)**: `std::towlower` in C++ on Windows does not apply French casing by default. Always use `::CharLowerBuffW(str.data(), len)` (from `user32.lib`) for case- and accent-insensitive searches.
- **File size formatting**: Use the system API `::StrFormatByteSizeW` (via `shlwapi.lib`) to respect the units localized by Windows (e.g. "1,42 Mo" in French, "1.42 MB" in English).
- **Mandatory update of interface translations**:
  The application has an external translation infrastructure (`.resw` / MRT Core) documented in [`TRANSLATIONS.md`](TRANSLATIONS.md).
  > [!IMPORTANT]
  > **Mandatory rule for the graphical interface**:
  > As soon as a text, label, button, dialog title, menu or tooltip is **created or modified** in the interface (XAML or C++), the agent **must**:
  > 1. Write the text in French: `Tr(L"Texte FR")` or `Fmt(Tr(L"… {0} …"), { value })` in C++ (never concatenated fragments around a value), plain text in the XAML (translated at load time by `UiTranslate.h`). **No hard-coded French text** in an interface setter (`box_value(L"…")`, `Text(L"…")`, `Content`, `Title`, `ShowStatus`…).
  > 2. Provide the `en`, `de`, `it`, `es`, `pt`, `nl` translations in a JSON file and merge them: `python tools\i18n-sync.py merge file.json` (the `T_<hash>` key is derived from the French label; official Evolution terminology: `evolution/po/*.po`).
  > 3. Verify with `python tools\i18n-sync.py check` (also run by `tools\run-tests.bat`).

---

## 7. Dynamic Task Management & Source of Truth

This `LLM.md` file deliberately contains **no static listing of done or to-do tasks**, to avoid any desynchronization. The project's status and features are driven dynamically by four key files that the AI agent must consult every time it takes over:

> [!IMPORTANT]
> **Living and evolving nature of the task files**:
> The files [`TODO.md`](TODO.md) and [`QUICKWINS.md`](QUICKWINS.md) **are never static or frozen**. Pierre may modify them or inject new tasks, requirements or priorities into them regularly as things progress. The AI agent must always re-read the real, up-to-date state of these files rather than rely on a recollection or an assumption.

### 7.1. Source files that must be read:
1. **[`VERSION`](VERSION)**: Indicates the project's current SemVer version (as well as the name of the active development branch, e.g. `0.9.8`).
2. **[`ROADMAP.md`](ROADMAP.md)**: **Strategic roadmap by version milestones** (v0.9.8, v1.0.0, v1.1.0, etc.). It tells the AI agent **which `TODO.md` tasks to handle first** for the current target version.
3. **[`CHANGELOG.md`](CHANGELOG.md)**: Official historical register listing all the features already completed, tested and validated, grouped by version.
4. **[`QUICKWINS.md`](QUICKWINS.md)**: List of priority tasks with high user impact and low implementation cost. This file evolves regularly with new additions from Pierre; the agent must always handle the open tasks (`- [ ]`) there with absolute priority.
5. **[`TODO.md`](TODO.md)**: Global, exhaustive reference of the project, detailing the full specifications, business constraints and progress status of each module. Pierre regularly adds new features to develop there.

### 7.2. Task handling rules for the AI agent:
- **Identifying needs**: The agent first consults [`ROADMAP.md`](ROADMAP.md) to identify the objectives of the active version milestone, then looks for the corresponding **unticked** tasks (`- [ ]`) in [`QUICKWINS.md`](QUICKWINS.md) and [`TODO.md`](TODO.md).
- **Mandatory update on completion**:
  - As soon as a task is developed and validated by the test suite (`tools\run-tests.bat`), it **must be ticked** (`- [x]`) in the relevant source files ([`ROADMAP.md`](ROADMAP.md), [`QUICKWINS.md`](QUICKWINS.md) and/or [`TODO.md`](TODO.md)).
  - The corresponding entry **must be recorded in [`CHANGELOG.md`](CHANGELOG.md)** under the `## [<version>] — en cours` section with a clear description of the functional and technical contribution.
  - The task gets a **dedicated atomic commit** before moving on to the next task.

---

## 8. Parallel Multi-Agent Work & Anti-Collision Strategy (Local Locks)

Several independent AI agents may work **in parallel on the local machine in this same workspace**. To avoid code collisions, regressions and concurrent modifications of the same files, each agent applies a lightweight local locking protocol via the [`locks/`](locks/) folder.

> [!NOTE]
> The `locks/*.json` files are excluded from versioning via [`.gitignore`](.gitignore). They live only on the local disk to coordinate agents in real time without polluting the Git history.

### 8.1. Local lock registry (`locks/`)
Each active agent signals its task by directly creating a JSON file on disk:
`locks/<agent_id>_<task_id>.json`

**Mandatory standardized JSON format for any lock:**
```json
{
  "agent_id": "Agent-Alpha",
  "task_ref": "ROADMAP.md §1 (v0.9.8) / TODO l.328",
  "description": "Validation de la synchronisation calendrier/contacts Microsoft 365",
  "files": [
    "winui/app/MainWindow.Calendar.cpp",
    "winui/app/MainWindow.Contacts.cpp",
    "winui/app/MainWindow.Sources.cpp"
  ],
  "functions_or_sections": "LoadSourcesAsync, SyncM365Calendars, UpdateContactCards",
  "started_at": "2026-11-04T07:30:00Z"
}
```

**Description of the JSON fields:**
- `agent_id` *(string)*: unique identifier of the working agent.
- `task_ref` *(string)*: explicit reference to the task in `ROADMAP.md`, `TODO.md` or `QUICKWINS.md`.
- `description` *(string)*: short summary, in French, of the functional goal.
- `files` *(array of strings)*: exhaustive list of the repository files the agent is about to modify.
- `functions_or_sections` *(string)*: names of the methods, classes or portions of code affected, to alert the other agents to the precise scope.
- `started_at` *(ISO 8601 string)*: UTC timestamp of when the task was taken on.

### 8.2. Mandatory protocol for each task (5-step cycle):

1. **Workspace check**:
   Make sure the local repository is clean via `git status` and that the branch is up to date.
2. **Non-collision check**:
   Read all the `.json` files present in the `locks/` directory:
   - If another agent has reserved the same files (or directly dependent files), **choose another independent task** in [`QUICKWINS.md`](QUICKWINS.md) or [`TODO.md`](TODO.md).
   - If a lock file is more than 4 hours old with no activity, it may be considered orphaned and cleaned up.
3. **Immediate placement of the local lock**:
   - Create the file `locks/<agent_id>_<task_id>.json` with the exact list of targeted files. No Git commit needed (the file is ignored).
4. **Development, Build & Tests**:
   - Make the code changes within the reserved scope.
   - Build the C engine (`.\tools\build-winui-core.bat`) and the application (`.\tools\build-winui-app.bat`).
   - Validate the entire test suite (`.\tools\run-tests.bat`). **100% success required**.
5. **Lock release, Documentation & Commit**:
   - **Delete your lock file** in `locks/`.
   - Tick the task in [`QUICKWINS.md`](QUICKWINS.md) or [`TODO.md`](TODO.md) (`- [x]`).
   - Add the descriptive entry in [`CHANGELOG.md`](CHANGELOG.md).
   - Commit the feature (`git add ... && git commit -m "feat(...): ..."`).
   - Push the commit to `origin/<active_branch>`.


