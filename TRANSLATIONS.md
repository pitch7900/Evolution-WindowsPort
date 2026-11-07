# Guide d'Internationalisation et des Traductions (I18n / L10n)

Ce document décrit le fonctionnement du système de traduction et d'internationalisation d'**EvolutionWin**, son architecture technique, la structure des répertoires impliqués et les règles à suivre lors de la modification ou de l'ajout d'éléments d'interface.

---

## 1. Vue d'Ensemble de l'Architecture

EvolutionWin repose sur l'infrastructure moderne **MRT Core** (Microsoft Windows App SDK) et sur les catalogues de ressources Windows natifs **`.resw`** :
1. Les chaînes traduites sont stockées dans des fichiers `Resources.resw` organisés par code de culture BCP-47 (`fr-FR`, `en-US`, `de-DE`, etc.).
2. Le projet MSBuild compile automatiquement ces ressources en un fichier binaire `resources.pri` via `makepri.exe` au moment de la génération.
3. Le loader C++/WinRT `EvolutionWin::I18n::GetString` (défini dans [`winui/app/I18n.h`](winui/app/I18n.h)) charge dynamiquement la chaîne appropriée à l'exécution en fonction de la langue active de l'utilisateur ou du système.
4. Le helper `EvolutionWin::Helpers::Tr(fr)` (ou `Tr(fr, en, resourceId)`) retrouve la traduction par la clé déduite du libellé français, avec repli sur le libellé source si elle manque. Les textes statiques du XAML sont traduits au chargement par `UiTranslate.h`.

---

## 2. Arborescence et Fichiers Impliqués

```
winui/
├── app/
│   ├── Strings/                               # Répertoire racine des catalogues .resw
│   │   ├── fr-FR/Resources.resw               # Français (langue source et de référence)
│   │   ├── en-US/Resources.resw               # Anglais (English US)
│   │   ├── de-DE/Resources.resw               # Allemand (Deutsch)
│   │   ├── it-IT/Resources.resw               # Italien (Italiano)
│   │   ├── es-ES/Resources.resw               # Espagnol (Español)
│   │   ├── pt-PT/Resources.resw               # Portugais (Português)
│   │   └── nl-NL/Resources.resw               # Néerlandais (Nederlands)
│   │
│   ├── I18n.h                                 # Wrapper MRT Core ResourceLoader
│   ├── MainWindow.Helpers.h                   # Helper Tr(fr, en, resourceId), UiIsEnglish()...
│   ├── Settings.h                             # Préférence "general.language"
│   ├── xaml/Settings.Appearance.xamlpart      # Sélecteur de langue dans l'interface
│   └── EvolutionWin.vcxproj                   # Déclaration des <PRIResource Include="..."/>
│
├── core/                                      # Moteur C (evo-core.dll)
│
tools/
└── msi/Evolution.wxs                          # Propriété WiX UILANGUAGE et registre HKLM
```

---

## 3. Identification des Textes : le Libellé Français est la Source

Chaque texte de l'interface est identifié par son **libellé français** (langue source). La clé de ressource est calculée automatiquement : `T_` suivi du hachage FNV-1a 32 bits (unités UTF-16) du libellé, en hexadécimal majuscule (`I18n::KeyFor` dans [`winui/app/I18n.h`](winui/app/I18n.h), même calcul dans [`tools/i18n-sync.py`](tools/i18n-sync.py)). **On n'écrit jamais ces clés à la main.** Chaque entrée des `.resw` porte le libellé français dans son `<comment>` pour la relecture.

Les anciennes clés nommées (`Nav_Mail`, `SpecialFolder_Inbox`, `FolderAction_*`, `Common_*`, `SearchScope_*`…) restent valables : on les utilise en passant le troisième argument de `Tr(fr, en, "Cle")`. Ce sont des exceptions ; la règle générale est la clé déduite du libellé.

---

## 4. Comment Utiliser les Traductions dans le Code

### 4.1. Dans le code C++ (`MainWindow.*.cpp`, etc.)

```cpp
#include "MainWindow.Helpers.h"
using namespace ::EvolutionWin::Helpers;

// Texte simple : le libellé français sert de clé (l'anglais éventuel n'est qu'un repli)
hstring label = Tr(L"Boîte de réception");
button.Content(box_value(Tr(L"Répondre à tous")));

// Phrase avec des valeurs : UN SEUL modèle avec marqueurs {0}, {1}…
// (jamais de fragments traduits concaténés autour d'une valeur : l'ordre des mots change selon la langue)
ShowStatus(Fmt(Tr(L"{0} message(s) supprimé(s) de {1}"), { to_hstring(count), folder_name }));
```

- Ne jamais comparer un texte **affiché** à un littéral français : utiliser le `Tag`, l'index ou une valeur interne.
- Ne pas traduire : clés de ressources XAML, valeurs de `Tag`, champs JSON, chemins, URL, formats techniques, messages de journal.

### 4.2. Dans les fichiers XAML (`.xamlpart`, `.xaml.in`, `*.xaml`)

On écrit simplement le **texte français** dans l'attribut (`Text`, `Content`, `Label`, `Header`, `PlaceholderText`, `Title`, `ToolTipService.ToolTip`, `AutomationProperties.Name`, texte d'un `Run`…) :
```xml
<AppBarButton Label="Nouveau message" />
```
`winui/app/UiTranslate.h` (`UiTranslate::ApplyToWindow`) parcourt l'arbre au chargement de chaque fenêtre et remplace le texte par sa traduction ; un texte inconnu reste en français. Pas d'`x:Uid`, pas de `x:Name` supplémentaire.

### 4.3. Outil `tools/i18n-sync.py`

| Commande | Effet |
|----------|-------|
| `python tools\i18n-sync.py check` | Vérifie que **chaque** texte source (appels `Tr(L"…")` sans clé explicite et textes XAML) existe dans les 7 catalogues avec les mêmes marqueurs `{n}` ; code 1 sinon. Exécuté par `tools\run-tests.bat`. |
| `python tools\i18n-sync.py missing` | Liste en JSON les textes absents d'au moins un catalogue. |
| `python tools\i18n-sync.py extract [fichiers]` | Liste en JSON les textes sources des fichiers donnés. |
| `python tools\i18n-sync.py merge traductions.json` | Ajoute aux catalogues les traductions `{"texte fr": {"en": "…", "de": "…", "it": "…", "es": "…", "pt": "…", "nl": "…"}}` (clé et commentaire générés). |

---

## 5. Règle Obligatoire lors de l'Ajout ou Modification d'Interface

> [!IMPORTANT]
> **Règle pour tout agent IA ou contributeur** :
> Dès qu'un libellé, bouton, menu, infobulle, message d'état ou dialogue est ajouté ou modifié :
> 1. L'écrire en français : `Tr(L"…")` / `Fmt(Tr(L"… {0} …"), {...})` en C++, texte brut en XAML. **Aucun texte français en dur** dans un setter d'interface.
> 2. Écrire un fichier JSON avec ses traductions `en`, `de`, `it`, `es`, `pt`, `nl` (terminologie officielle d'Evolution : `evolution/po/*.po`) et le fusionner : `python tools\i18n-sync.py merge fichier.json`.
> 3. Vérifier : `python tools\i18n-sync.py check` (et `tools\run-tests.bat`).
> 4. Contrôle rapide des textes oubliés : `grep -rnF --include="*.cpp" --include="*.h" -e 'box_value(L"' -e 'Text(L"' winui/app` ne doit plus montrer que des clés techniques (brosses, styles…).
