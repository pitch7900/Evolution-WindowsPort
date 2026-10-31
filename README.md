<!-- cspell:disable -->
# Evolution pour Windows 11 (Portage natif MSVC & WinUI 3)

Ce dépôt contient le portage **100 % natif sous Windows 11** du client de messagerie, calendrier et carnet d'adresses open source **GNOME Evolution**, compilé avec **Microsoft Visual C++ (MSVC)** (sans MinGW, sans WSL et sans Cygwin) et doté d'une **interface utilisateur native WinUI 3 (`EvolutionWin`)**.

Pour le journal technique détaillé, l'historique des correctifs CMake/MSVC et le suivi des phases, consultez [**WINDOWS-PORT.md**](./WINDOWS-PORT.md).

---

## Architecture du projet

Le projet s'articule autour de deux couches principales :

1. **Moteur de messagerie, d'agenda et de contacts (`evolution-data-server`, `Camel` et `evo-core.dll`)** :
   - Compilation native MSVC (x64) d'**Evolution Data Server (EDS)** et de **Camel** (IMAP, POP3, SMTP, dossiers locaux, S/MIME via NSS).
   - Intégration native avec le **Gestionnaire d'identifiants Windows** (`CredWriteW` / `CredReadW` / `CredDeleteW`, protection DPAPI) en remplacement de `libsecret`.
   - Démarrage automatique sans fenêtre console des services D-Bus d'EDS (`evolution-source-registry`, fabriques carnet d'adresses et agenda).
   - Bibliothèque pont **`evo-core.dll`** (`winui/core/`) exposant une API C asynchrone avec sérialisation JSON vers l'interface graphique.

2. **Interface native Windows 11 (`EvolutionWin.exe` — WinUI 3 / C++/WinRT)** :
   - Contrôles natifs Windows App SDK (`NavigationView`, `TreeView`, `ListView`, `CalendarView`, `AutoSuggestBox`, `ContentDialog`) avec rendu **Mica** et support du thème système (clair/sombre).
   - **Lecture et rédaction HTML** propulsées par **Microsoft Edge WebView2** (barre d'outils de mise en forme riche, bascule HTML / texte brut, blocage du contenu distant dans le lecteur).
   - **Impression native** des messages via WebView2 (`ShowPrintUI`).
   - **Notifications système Windows (Toasts)** à l'arrivée de nouveaux messages, gestion des **pièces jointes**, **recherche** instantanée et plein texte, **calendrier** et **contacts** EDS.
   - Une version GTK3 transitoire (`evolution.exe` + shim `webkit2gtk-4.1.dll` sur WebView2) est également disponible.

---

## Organisation du dépôt

| Répertoire / Fichier | Rôle |
| --- | --- |
| [`WINDOWS-PORT.md`](./WINDOWS-PORT.md) | Documentation technique complète et suivi d'avancement du portage |
| [`winui/`](./winui) | Client natif Windows 11 : `winui/core` (`evo-core.dll`) et `winui/app` (`EvolutionWin.exe`) |
| [`tools/`](./tools) | Scripts de compilation MSVC (`build-*.bat`, `env.bat`, `check-prereqs.ps1`), couche CMake (`cmake/msvc-compat.cmake`), en-têtes POSIX de compatibilité (`compat/`), shim WebKit2 (`webkit-shim/`) et utilitaires (`tools/nuget.exe`) |
| [`build/`](./build) | Exécutables finaux prêts à l'emploi (`EvolutionWin.exe`, `evo-core.dll`, `evolution.exe`, `libexec/*.exe`) et sous-dossier `build/intermediate/` (fichiers d'objets CMake/MSBuild, ignoré par Git) |
| [`evolution/`](./evolution) | Code source d'Evolution adapté pour MSVC |
| [`evolution-data-server/`](./evolution-data-server) | Code source d'Evolution Data Server adapté pour Windows 11 / MSVC |
| [`deps/`](./deps) | Sources tierces téléchargées automatiquement (NSS, NSPR, libical, SDK WebView2) |
| [`gtk-build/`](./gtk-build) | Pile GTK3 / GLib / libsoup3 compilée avec `gvsbuild` et préfixe d'installation commun (`gtk-build/gtk/x64/release`, ignoré par Git) |

---

## Démarrage rapide

### 1. Vérifier et installer les prérequis

Depuis une invite de commandes à la racine du dépôt :

```bat
tools\check-prereqs.bat            :: Vérifie l'environnement et affiche un rapport
tools\check-prereqs.bat -Install   :: Installe/télécharge automatiquement ce qui manque
```

### 2. Compiler le projet (dans l'ordre)

```bat
tools\build-gtk.bat          :: Pile GTK3 / GLib via gvsbuild (1 à 2 h la première fois)
tools\build-nss.bat          :: NSPR + NSS (S/MIME et TLS)
tools\build-libical.bat      :: libical + libical-glib (fuseaux horaires intégrés)
tools\build-eds.bat          :: evolution-data-server
tools\build-webkit-shim.bat  :: Shim WebKitGTK 4.1 sur WebView2 (pour la version GTK)
tools\build-evolution.bat    :: Evolution (version GTK transitoire)
tools\build-winui-core.bat   :: evo-core.dll (moteur C pour le client WinUI 3)
tools\build-winui-app.bat    :: EvolutionWin.exe (client natif WinUI 3, généré dans .\build)
```

### 3. Lancer le client natif Windows 11

```bat
tools\run-winui.bat          :: Lance .\build\EvolutionWin.exe avec le préfixe dans le PATH
tools\run-winui-debug.bat    :: Lance EvolutionWin.exe avec journaux de débogage IMAP/evo-core
```

