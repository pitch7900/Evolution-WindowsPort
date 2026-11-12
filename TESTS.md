<!-- cspell:disable -->
# Unit and regression testing guide

This document details the organization, execution and extension of the unit test suite of **Evolution-WindowsPort**, intended to validate non-regression between each version on Windows 11 with MSVC.

---

## 1. Philosophy and best practices

The test suite was designed following best practices for native C / C++ projects on Windows:

1. **Autonomy and zero external dependencies**: no heavy download (GoogleTest, Boost) is needed. A lightweight header-only micro-framework ([`tests/include/test_framework.h`](./tests/include/test_framework.h)) provides clear assertions (`ASSERT_TRUE`, `ASSERT_EQ_STR`, etc.) and compiles instantly with the native MSVC compiler (`cl.exe`).
2. **Determinism and isolation**: unit tests run entirely locally, with no network connection, without requiring an external mail server or modifying user data (`%LOCALAPPDATA%`).
3. **CI/CD automation and pre-release**: the test script returns a standardized exit code (`0` on success, `1` on failure), making it easy to integrate into GitHub Actions, pre-commit hooks or before publishing a Release (`tools\publish-release.bat`).
4. **Binary ABI verification**: a dynamic export test uses `dumpbin` to verify that no public symbol of the `evo-core.dll` shared library was omitted or broken during compilation.

---

## 2. Test tree layout

```text
Evolution-WindowsPort/
├── tests/
│   ├── include/
│   │   └── test_framework.h           # Assertion and summary micro-framework for C/C++
│   ├── unit/
│   │   ├── test_helpers.cpp           # Unit tests of WinUI algorithms and utilities
│   │   ├── test_evo_core_contracts.c  # Tests of JSON interface contracts and evo-core robustness
│   │   └── test_settings.cpp          # Tests of the preferences manager and JSON resilience
│   ├── abi/
│   │   └── test_abi_exports.ps1       # Symbol export validation of evo-core.dll
│   └── CMakeLists.txt                 # Optional CMake / CTest configuration
└── tools/
    └── run-tests.bat                  # Unified build and execution script
```

---

## 3. Test suite details

### Suite 1: WinUI algorithms and utilities (`test_helpers.cpp`)
Validates the pure functions from `MainWindow.Helpers.h`:
- **Sender extraction (`SenderName`)**: support for RFC 822 / 5322 formats (`"Name <email>"`, `"\"Name, First\" <email>"`, bare addresses, empty strings).
- **File name sanitization (`SanitizeFileName`)**: strict replacement of characters forbidden on Windows (`\ / : * ? " < > |`) and ASCII control characters (< 32), with a fallback.
- **Reply all (`WithoutAddress`)**: exclusion of the user's address from the recipient list, case insensitivity, handling of whitespace and multi-address lists.
- **HTML safety (`EscapeHtml`)**: escaping of special characters (`&`, `<`, `>`, `"`) and conversion of CRLF line breaks to `<br>` to prevent any corruption or injection when quoting.
- **Email quotes (`Quote`)**: formatting of text blocks with the `> ` prefix and handling of CRLF line endings.
- **Semantic version comparison (`SemVer`)**: guarantees a strict numeric comparison (`x.y.z`) for the GitHub update lookup mechanism, avoiding the textual pitfall where `"0.9.10"` would be considered lower than `"0.9.9"`.
- **Lexical detection of a missing attachment (`HasAttachmentKeywords`)**: multilingual (French, English) and case-insensitive analysis of explicit attachment mentions ("pièce jointe", "ci-joint", "en pj", "attachment", etc.) to alert the user before sending.
- **Standalone HTML generation for the browser (`BuildStandaloneHtmlMessage`)**: construction of a complete document (RFC headers, metadata, HTML body or escaped plain text in `<pre>`) for display in the system browser.
- **Text extraction and cleanup for speech synthesis (`ExtractPlainTextForSpeech`)**: ordered extraction (sender, subject, body), filtering of HTML tags and decoding of common entities for smooth speech reading.
- **Secure attachment preview classification (`DetectAttachmentPreviewKind`)**: strict typing of file extensions (WIC-decodable images, plain text and code files not interpreted, executable or risky types protected without preview).
- **Contact suggestion formatting (`FormatContactSuggestion`)**: elegant presentation of contacts ("Name <email>" or fallback to the available field) for quick search in the ribbon's AutoSuggestBox.
- **Calendar slot height (`GetCalendarHourHeight`)**: validation of display height calculations according to the time step (60 min = 48px, 30 min = 72px, 15 min = 120px).
- **Temporary file cleanup (`PurgeDirectoryFiles`)**: validation of cache directory purging and computation of the space freed.
- **Second time zone handling (`GetTimezoneUtcOffsetHours`, `GetTimezoneShortLabel`, `CalculateEquivalentHour`)**: UTC offsets, short abbreviations and hour conversion with midnight rollover handling.
- **Multi-criteria and category search (`MatchesMessageSearch`)**: support for the `cat:` and `category:` syntaxes and detection of label keywords in local and cross-folder search.
- **Reading text size (`FontSizePresetToZoom`)**: mapping of the _Small / Normal / Large / Very large_ choices to the reader zoom factor (0.85 / 1.0 / 1.25 / 1.5), falling back to 1.0 for an unknown value.

### Suite 2: `evo-core` interface contracts and robustness (`test_evo_core_contracts.c`)
Verifies the robustness of the C engine and its exchanges with the interface:
- **Splitting of Windows Credential Manager secrets**: validation of the partitioning and reassembly algorithm for large OAuth2 tokens (> 2,560 bytes), eliminating the `CredWrite` error that occurred in v0.9.4 with Entra ID.
- **Syntactic validation of addresses**: format check before querying the DNS and Autodiscover resolvers.
- **Special folder priority**: compliance with Outlook's standardized classification and display order (Inbox, Drafts, Sent Items, Deleted Items, Archive, Junk Email, local Outbox).
- **JSON event schemas**: verification of the presence of required fields for the `progress`, `connection-status`, `folder-changed` and `new-mail` events.
- **Web calendar addresses**: classification of addresses entered for `evo_core_create_calendar` (`http(s)://`, `webcal://`, empty = local calendar, other schemes rejected).

### Suite 3: Application preferences (`test_settings.cpp`)
Tests the persistence of the `settings.json` file:
- Reading back with guaranteed default values (fallback).
- Modification and persistence of numbers, booleans and strings.
- Handling of JSON arrays (list of trusted senders and domains for external content blocking).
- Resilience against syntactic corruption of a malformed JSON file.
- **Mail and security options (Quickwins)**: validation of the mark-as-read modes (`immediate`, `delayed`, `on-selection-change`, `never`), trash purge on exit (`mail.empty-trash-on-exit`), permanent deletion confirmations, folder pane modes (`layout.folder-pane`: `normal`, `minimized`, `off`) and folder sorting (`mail.folder-sort`: `special-first`, `alphabetical`).

### Suite 4: Binary integrity and ABI export (`test_abi_exports.ps1`)
- Dynamically extracts all public function declarations from [`winui/core/evo-core.h`](./winui/core/evo-core.h) (`evo_core_*`).
- Queries the exports of [`build/evo-core.dll`](./build/evo-core.dll) via `dumpbin /exports`.
- Immediately reports any function that is missing or not exported under MSVC.

---

## 4. Running the tests

### In one command (recommended)

Open a Windows command prompt (`cmd.exe`) at the project root and run:

```bat
tools\run-tests.bat
```

This script:
1. Loads the MSVC compiler environment via `tools\env.bat`.
2. Compiles the test executables into `build\tests\`.
3. Runs each test suite in turn with detailed output.
4. Validates the DLL symbol export.
5. Returns `0` if all tests passed, or `1` in case of failure.

### Via CMake / CTest (optional)

If you use CMake to orchestrate your builds:

```bat
call tools\env.bat
cmake -S tests -B build\intermediate\tests -G Ninja
cmake --build build\intermediate\tests
ctest --test-dir build\intermediate\tests --output-on-failure
```

---

## 5. How to add a new test

To add a regression test while developing a new feature:

1. Open the relevant suite in `tests/unit/` (or create a new one).
2. Use the framework macros:
   ```cpp
   TEST_CASE("Explicit name of the feature to validate");
   {
       int result = MyFunction(param1, param2);
       ASSERT_EQ_INT(42, result);
       ASSERT_TRUE(CheckedCondition());
   }
   TEST_CASE_END();
   ```
3. Run `tools\run-tests.bat` to check that the test passes right away.
