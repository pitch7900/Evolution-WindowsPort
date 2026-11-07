# Guide d'Internationalisation et des Traductions (I18n / L10n)

Ce document décrit le fonctionnement du système de traduction et d'internationalisation d'**EvolutionWin**, son architecture technique, la structure des répertoires impliqués et les règles à suivre lors de la modification ou de l'ajout d'éléments d'interface.

---

## 1. Vue d'Ensemble de l'Architecture

EvolutionWin repose sur l'infrastructure moderne **MRT Core** (Microsoft Windows App SDK) et sur les catalogues de ressources Windows natifs **`.resw`** :
1. Les chaînes traduites sont stockées dans des fichiers `Resources.resw` organisés par code de culture BCP-47 (`fr-FR`, `en-US`, `de-DE`, etc.).
2. Le projet MSBuild compile automatiquement ces ressources en un fichier binaire `resources.pri` via `makepri.exe` au moment de la génération.
3. Le loader C++/WinRT `EvolutionWin::I18n::GetString` (défini dans [`winui/app/I18n.h`](winui/app/I18n.h)) charge dynamiquement la chaîne appropriée à l'exécution en fonction de la langue active de l'utilisateur ou du système.
4. Le helper `EvolutionWin::Helpers::Tr(fr, en, resourceId)` assure un chargement prioritaire depuis le catalogue de ressources avec repli en mémoire immédiat si nécessaire.

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

## 3. Conventions de Nommage des Clés de Ressource

Les identifiants (`name` dans les fichiers `.resw`) doivent être explicites et catégorisés par préfixe :
- `Nav_<Module>` : Rubriques de navigation principale (ex: `Nav_Mail`, `Nav_Calendar`).
- `SpecialFolder_<Type>` : Dossiers système de messagerie (ex: `SpecialFolder_Inbox`, `SpecialFolder_Sent`).
- `FolderAction_<Action>` : Actions sur les dossiers dans l'arborescence (ex: `FolderAction_NewFolder`, `FolderAction_Rename`).
- `Common_<Action>` : Boutons et actions usuelles (ex: `Common_Cancel`, `Common_Delete`, `Common_Save`).
- `SearchScope_<Scope>` : Portées et champs de recherche (ex: `SearchScope_Subfolders`, `SearchScope_Folder`).
- `Dialog_<Sujet>_<Element>` : Textes des boîtes de dialogue et messages de confirmation.

---

## 4. Comment Utiliser les Traductions dans le Code

### 4.1. Dans le code C++ (`MainWindow.*.cpp`, etc.)

Pour afficher un texte dans l'interface, utiliser systématiquement la fonction `Tr()` définie dans `MainWindow.Helpers.h` en lui passant la clé de ressource `.resw` :

```cpp
#include "MainWindow.Helpers.h"
using namespace ::EvolutionWin::Helpers;

// 1. Appel recommandé avec clé .resw (charge depuis resources.pri, repli sur fr ou en) :
hstring label = Tr(L"Boîte de réception", L"Inbox", L"SpecialFolder_Inbox");

// 2. Appel direct via le loader MRT Core (si le texte n'a pas de repli en dur) :
#include "I18n.h"
hstring titre = ::EvolutionWin::I18n::GetString(L"Dialog_Confirm_Title", L"Confirmation");
```

### 4.2. Dans les fichiers XAML (`.xamlpart`, `.xaml.in`)

Pour les contrôles déclarés en XAML, associer une directive `x:Uid` au contrôle correspondant :
```xml
<TextBlock x:Uid="Nav_Mail" Text="Courrier" />
```
Et dans les fichiers `.resw`, définir :
- `Nav_Mail.Text` = `Courrier` (fr-FR) / `Mail` (en-US) / `E-Mail` (de-DE)...

---

## 5. Règle Obligatoire lors de l'Ajout ou Modification d'Interface

> [!IMPORTANT]
> **Règle pour tout agent IA ou contributeur** :
> Dès qu'un nouveau libellé, bouton, menu, infobulle ou dialogue est ajouté ou modifié dans l'interface :
> 1. Définir une clé de ressource claire dans `winui/app/Strings/fr-FR/Resources.resw`.
> 2. Répercuter la clé avec sa traduction équivalente dans **tous les autres catalogues** :
>    - `en-US/Resources.resw`
>    - `de-DE/Resources.resw`
>    - `it-IT/Resources.resw`
>    - `es-ES/Resources.resw`
>    - `pt-PT/Resources.resw`
>    - `nl-NL/Resources.resw`
> 3. Utiliser les traductions officielles du projet Evolution GNOME comme référence terminologique (disponibles dans `evolution/po/*.po`).
> 4. Raccorder le code C++ ou XAML en utilisant `Tr(fr, en, resourceId)` ou `x:Uid`.
> 5. Valider avec `tools\build-winui-app.bat` et `tools\run-tests.bat`.
