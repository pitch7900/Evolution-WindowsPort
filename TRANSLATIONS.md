# Internationalization and Translation Guide (I18n / L10n)

This document describes how the translation and internationalization system of **EvolutionWin** works, its technical architecture, the structure of the directories involved, and the rules to follow when changing or adding user interface elements.

---

## 1. Architecture Overview

EvolutionWin relies on the modern **MRT Core** infrastructure (Microsoft Windows App SDK) and on the native Windows **`.resw`** resource catalogs:
1. Translated strings are stored in `Resources.resw` files organized by BCP-47 culture code (`fr-FR`, `en-US`, `de-DE`, etc.).
2. The MSBuild project automatically compiles these resources into a binary `resources.pri` file via `makepri.exe` at build time.
3. The C++/WinRT loader `EvolutionWin::I18n::GetString` (defined in [`winui/app/I18n.h`](winui/app/I18n.h)) dynamically loads the appropriate string at run time, based on the active language of the user or the system.
4. The helper `EvolutionWin::Helpers::Tr(fr)` (or `Tr(fr, en, resourceId)`) finds the translation by the key derived from the French label, falling back to the source label if it is missing. Static XAML texts are translated at load time by `UiTranslate.h`.

---

## 2. Directory Tree and Files Involved

```
winui/
├── app/
│   ├── Strings/                               # Root directory of the .resw catalogs
│   │   ├── fr-FR/Resources.resw               # French (source and reference language)
│   │   ├── en-US/Resources.resw               # English (English US)
│   │   ├── de-DE/Resources.resw               # German (Deutsch)
│   │   ├── it-IT/Resources.resw               # Italian (Italiano)
│   │   ├── es-ES/Resources.resw               # Spanish (Español)
│   │   ├── pt-PT/Resources.resw               # Portuguese (Português)
│   │   └── nl-NL/Resources.resw               # Dutch (Nederlands)
│   │
│   ├── I18n.h                                 # MRT Core ResourceLoader wrapper
│   ├── MainWindow.Helpers.h                   # Helper Tr(fr, en, resourceId), UiIsEnglish()...
│   ├── Settings.h                             # "general.language" preference
│   ├── xaml/Settings.Appearance.xamlpart      # Language selector in the interface
│   └── EvolutionWin.vcxproj                   # Declaration of <PRIResource Include="..."/>
│
├── core/                                      # C engine (evo-core.dll)
│
tools/
└── msi/Evolution.wxs                          # WiX UILANGUAGE property and HKLM registry
```

---

## 3. Identifying Texts: the French Label Is the Source

Each interface text is identified by its **French label** (source language). The resource key is computed automatically: `T_` followed by the 32-bit FNV-1a hash (UTF-16 units) of the label, in uppercase hexadecimal (`I18n::KeyFor` in [`winui/app/I18n.h`](winui/app/I18n.h), same computation in [`tools/i18n-sync.py`](tools/i18n-sync.py)). **These keys are never written by hand.** Each `.resw` entry carries the French label in its `<comment>` for review.

The old named keys (`Nav_Mail`, `SpecialFolder_Inbox`, `FolderAction_*`, `Common_*`, `SearchScope_*`…) remain valid: use them by passing the third argument of `Tr(fr, en, "Key")`. These are exceptions; the general rule is the key derived from the label.

---

## 4. How to Use Translations in the Code

### 4.1. In C++ code (`MainWindow.*.cpp`, etc.)

```cpp
#include "MainWindow.Helpers.h"
using namespace ::EvolutionWin::Helpers;

// Simple text: the French label serves as the key (any English text is only a fallback)
hstring label = Tr(L"Boîte de réception");
button.Content(box_value(Tr(L"Répondre à tous")));

// Sentence with values: ONE SINGLE template with {0}, {1}… placeholders
// (never translated fragments concatenated around a value: word order changes from one language to another)
ShowStatus(Fmt(Tr(L"{0} message(s) supprimé(s) de {1}"), { to_hstring(count), folder_name }));
```

- Never compare a **displayed** text to a French literal: use the `Tag`, the index, or an internal value.
- Do not translate: XAML resource keys, `Tag` values, JSON fields, paths, URLs, technical formats, log messages.

### 4.2. In XAML files (`.xamlpart`, `.xaml.in`, `*.xaml`)

Simply write the **French text** in the attribute (`Text`, `Content`, `Label`, `Header`, `PlaceholderText`, `Title`, `ToolTipService.ToolTip`, `AutomationProperties.Name`, text of a `Run`…):
```xml
<AppBarButton Label="Nouveau message" />
```
`winui/app/UiTranslate.h` (`UiTranslate::ApplyToWindow`) walks the tree when each window loads and replaces the text with its translation; an unknown text stays in French. No `x:Uid`, no additional `x:Name`.

### 4.3. The `tools/i18n-sync.py` tool

| Command | Effect |
|---------|--------|
| `python tools\i18n-sync.py check` | Checks that **every** source text (`Tr(L"…")` calls without an explicit key, and XAML texts) exists in the 7 catalogs with the same `{n}` placeholders; exit code 1 otherwise. Run by `tools\run-tests.bat`. |
| `python tools\i18n-sync.py missing` | Lists, as JSON, the texts missing from at least one catalog. |
| `python tools\i18n-sync.py extract [files]` | Lists, as JSON, the source texts of the given files. |
| `python tools\i18n-sync.py merge traductions.json` | Adds to the catalogs the translations `{"texte fr": {"en": "…", "de": "…", "it": "…", "es": "…", "pt": "…", "nl": "…"}}` (key and comment generated). |

---

## 5. Mandatory Rule When Adding or Changing the Interface

> [!IMPORTANT]
> **Rule for any AI agent or contributor**:
> Whenever a label, button, menu, tooltip, status message or dialog is added or changed:
> 1. Write it in French: `Tr(L"…")` / `Fmt(Tr(L"… {0} …"), {...})` in C++, plain text in XAML. **No hard-coded French text** in an interface setter.
> 2. Write a JSON file with its `en`, `de`, `it`, `es`, `pt`, `nl` translations (official Evolution terminology: `evolution/po/*.po`) and merge it: `python tools\i18n-sync.py merge file.json`.
> 3. Verify: `python tools\i18n-sync.py check` (and `tools\run-tests.bat`).
> 4. Quick check for forgotten texts: `grep -rnF --include="*.cpp" --include="*.h" -e 'box_value(L"' -e 'Text(L"' winui/app` must only show technical keys (brushes, styles…).
