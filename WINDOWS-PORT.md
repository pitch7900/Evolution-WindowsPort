<!-- cspell:disable -->
# Portage natif Windows 11 (MSVC) d'Evolution

Journal de travail du fork `windows-native` (`Evolution-WindowsPort`). Objectif : compiler et faire
tourner Evolution **nativement** sous Windows 11 avec le compilateur Microsoft
Visual C++ (pas de MinGW, pas de WSL, pas de Cygwin) et proposer une interface
native **WinUI 3** (`EvolutionWin`).

## Démarrage rapide

### 1. Récupérer les sources

Ce dépôt regroupe à sa racine le code source d'**Evolution** (`evolution/`) et
d'**evolution-data-server (EDS)** (`evolution-data-server/`), ainsi que le
client natif WinUI 3 (`winui/`) et les outils de portage (`tools/`).

Les sources amont sont versionnées directement dans ce dépôt (monodépôt) :
un premier commit importe Evolution (commit amont `be33fb6204`) et EDS
(commit amont `ea6b0124d`) tels quels, le suivant applique les modifications
Windows. `git diff <import>..<commit Windows> -- evolution` donne donc le
correctif complet. evolution-ews (commit amont `19cb82b3`) est importé de la
même façon (`evolution-ews\.git-upstream` en local).

Sur le poste de développement, l'historique amont (GNOME) des deux projets
est conservé localement dans `evolution\.git-upstream` et
`evolution-data-server\.git-upstream` (ignorés par Git, branche
`windows-native`), pour suivre l'amont :

```bat
git --git-dir=evolution-data-server\.git-upstream --work-tree=evolution-data-server fetch origin
```

Les modifications Windows d'EDS sont également conservées sous forme de
patch (`tools/patches/evolution-data-server.patch`, établi sur le
commit EDS `ea6b0124d`). Si EDS doit être recloné depuis l'amont,
`tools\check-prereqs.bat -Install` le clone et applique ce patch.

### 2. Vérifier les prérequis

```bat
tools\check-prereqs.bat            :: rapport seulement
tools\check-prereqs.bat -Install   :: installe/télécharge ce qui manque
```

Le script contrôle les outils (Visual Studio 2022 + C++, SDK Windows, Git,
CMake, Python et les paquets `gvsbuild`/`meson`/`ninja`/`gyp-next`, MSYS2 avec
`make`/`patch` et `intltool`/`perl`, runtime WebView2), les sources (EDS, NSS, NSPR, libical, SDK
WebView2) et l'état des compilations. Avec `-Install`, les outils manquants
sont installés via `winget`/`pip` (Visual Studio Build Tools demande une
élévation UAC) et les sources sont clonées/téléchargées. Il ne lance aucune
compilation. Code de retour : 0 si tous les prérequis obligatoires sont
présents, 1 sinon.

### 3. Compiler, dans cet ordre

```bat
tools\build-gtk.bat         :: pile GTK3 via gvsbuild (1 à 2 h la première fois)
tools\build-nss.bat         :: NSPR + NSS
tools\build-libical.bat     :: libical + libical-glib
tools\build-eds.bat         :: evolution-data-server
tools\build-ews.bat         :: evolution-ews (Microsoft 365 / Exchange, parties EDS)
tools\build-webkit-shim.bat :: API WebKitGTK sur WebView2 (phase 3)
tools\build-evolution.bat   :: Evolution (version GTK transitoire)
tools\build-winui-core.bat  :: evo-core.dll (moteur C pour le client WinUI 3)
tools\build-winui-app.bat   :: EvolutionWin.exe (client natif WinUI 3, généré dans .\build)
tools\run-winui.bat         :: lance .\build\EvolutionWin.exe avec le bon PATH
tools\build-msi.bat         :: installeur dist\Evolution-<version>-x64.msi (phase 5)
```

## Arborescence

Tous les scripts déduisent leurs chemins de leur propre emplacement
(`tools\env.bat`) ; aucun chemin propre à une machine n'y est codé.

| Chemin | Rôle | Variable (surchargeable) |
| --- | --- | --- |
| `<workspace>` | racine de ce dépôt (`Evolution-WindowsPort`) | `EVO_WORKSPACE` |
| `<workspace>\winui` | client natif Windows 11 (`winui\core` : `evo-core.dll`, `winui\app` : `EvolutionWin.exe`) | `WINUI_SRC` |
| `<workspace>\tools` | scripts et outils de portage MSVC (`build-*.bat`, `env.bat`, `check-prereqs.ps1`, `cmake/`, `compat/`, `patches/`, `pkgconfig/`, `webkit-shim/`, `tools/nuget.exe`) | `MSVC_SCRIPTS` |
| `<workspace>\build` | exécutables finaux prêts à l'emploi (`EvolutionWin.exe`, `evo-core.dll`, `evo-core-test.exe`, `evolution.exe`, `libexec\*.exe`) et sous-dossier `build\intermediate` (fichiers d'objets CMake/MSBuild, ignoré par Git) | `BUILD_DIR` / `BUILD_INTERMEDIATE` |
| `<workspace>\evolution` | code source d'Evolution, branche `windows-native` | `EVO_SRC` |
| `<workspace>\evolution-data-server` | code source d'EDS, branche `windows-native` | `EDS_SRC` |
| `<workspace>\evolution-ews` | code source d'evolution-ews (Microsoft 365 / Exchange), branche `windows-native` | `EWS_SRC` |
| `<workspace>\deps` | sources tierces (NSS, NSPR, libical, SDK WebView2) | `DEPS` |
| `<workspace>\gtk-build` | dossier de la pile GTK compilée par gvsbuild (ignoré par Git) | `GTK_BUILD` |
| `<workspace>\gtk-build\gtk\x64\release` | préfixe d'installation commun de toutes les dépendances | `PREFIX` (dérivé de `GTK_BUILD`) |
| `C:\msys64` | MSYS2, utilisé **uniquement comme fournisseur d'outils** (patch, make, bash) par gvsbuild et le build de NSPR. Aucun binaire produit ne dépend de MSYS2. | `MSYS2_ROOT` |

Visual Studio 2022 est localisé automatiquement avec `vswhere` (toutes éditions).

## Choix d'architecture

| Composant Linux | Remplacement Windows |
| --- | --- |
| WebKitGTK (`webkit2gtk-4.1`) — affichage des mails, éditeur, aperçus | **Microsoft WebView2** (Edge/Chromium) intégré dans WinUI 3 (`EvolutionWin`) et dans un widget GTK (phase 3) |
| libsecret (Secret Service D-Bus) | **Windows Credential Manager** (`CredWriteW`/`CredReadW`/`CredDeleteW`) |
| libuuid | `g_uuid_string_random()` de GLib |
| D-Bus session bus (services EDS) | bus de session intégré à GLib sous Windows (adresse `autolaunch:`, démarré par `gdbus.exe`). Il ne gère pas l'activation par fichiers `.service` : EDS démarre lui-même `evolution-source-registry` et les fabriques. |

## Outillage

| Outil | Version testée | Installation (`check-prereqs -Install`) |
| --- | --- | --- |
| Visual Studio 2022, charge « Développement Desktop en C++ » | MSVC 14.44.35207 | `winget install Microsoft.VisualStudio.2022.BuildTools` + charge VCTools |
| Windows SDK | 10.0.26100.0 | avec Visual Studio |
| Git | 2.56 | `winget install Git.Git` |
| CMake (≥ 3.15) | 4.4.5 | `winget install Kitware.CMake` |
| Python (≥ 3.9) | 3.13.7 | `winget install Python.Python.3.13` |
| gvsbuild, meson, ninja, gyp-next | 2026.10.0, 1.12.1, 1.13.2 | `python -m pip install --user ...` |
| MSYS2 (+ `make`, `patch`) | 2026-09-27 | `winget install MSYS2.MSYS2` |
| Runtime Edge WebView2 | 156.0.4314.50 | fourni avec Windows 11 (`winget install Microsoft.EdgeWebView2Runtime` sinon) |

## Phase 0 — Pile GTK3 compilée avec MSVC

Script : `tools\build-gtk.bat` (appelle
`gvsbuild build --fast-build --vs-ver vs2022 --configuration release` avec la
liste des projets : gtk3, libsoup3, glib-networking, libxml2, sqlite, enchant,
json-glib, gsettings-desktop-schemas, adwaita-icon-theme, hicolor-icon-theme,
librsvg, pkgconf, icu, libarchive, openssl).

### Problèmes rencontrés

1. **`'create-lists.bat' n'est pas reconnu`** pendant le build de gettext.
   Cause : la variable d'environnement `NoDefaultCurrentDirectoryInExePath=1`
   empêche `cmd.exe` d'exécuter un script du répertoire courant, ce dont
   dépendent les Makefile nmake de gettext. Solution : `env.bat` vide cette
   variable pour tous les scripts de build.

## Organisation des scripts (`tools/`)

Tout ce qui concerne l'outillage et le portage MSVC est regroupé dans `tools/` à
la racine du dépôt. Le dossier `evolution/win32/` contenait un ancien portage
**MinGW** (2014), abandonné faute de WebKit2 sous Windows (voir
`evolution/win32/readme.txt`) ; il n'est pas utilisé.

| Fichier | Rôle |
| --- | --- |
| `env.bat` | Environnement commun : chemins relatifs, Visual Studio (vswhere), PATH, pkg-config |
| `check-prereqs.bat` / `check-prereqs.ps1` | Vérification des prérequis, installation automatique avec `-Install` |
| `build-gtk.bat` | Pile GTK3 et dépendances via gvsbuild |
| `build-nss.bat` + `build-nss.sh` | NSPR + NSS (installe aussi `nss.pc`/`nspr.pc`) |
| `build-libical.bat` | libical + libical-glib |
| `build-eds.bat` | evolution-data-server |
| `build-ews.bat` | evolution-ews (Microsoft 365 / Exchange), parties EDS seulement |
| `build-webkit-shim.bat` | Shim WebKitGTK 4.1 sur WebView2 |
| `build-evolution.bat` | Evolution (version GTK transitoire) |
| `build-winui-core.bat` | `evo-core.dll` + `evo-core-test.exe` |
| `build-winui-app.bat` | `EvolutionWin.exe` (client natif WinUI 3 dans `.\build`) |
| `run-winui.bat` / `run-winui-debug.bat` | Lancement de `.\build\EvolutionWin.exe` (normal ou avec traces de débogage) |
| `build-msi.bat` + `build-msi.ps1`, `msi/Evolution.wxs` | Installeur MSI autonome dans `dist\` (WiX v5) |
| `cmake/msvc-compat.cmake` | Couche CMake injectée dans tous les projets |
| `patches/evolution-data-server.patch` | Modifications Windows d'EDS |
| `pkgconfig/*.pc.in` | Modèles pkg-config pour NSS/NSPR/ICU/sqlite3 (`@PREFIX@` remplacé à l'installation) |
| `nspr-config.site`, `tools/nsinstall`, `tools/patch-nspr.py`, `tools/nuget.exe` | Aides au build de NSPR et restauration NuGet |
| `webkit-api-usage.txt` | Inventaire des appels WebKit/JSC utilisés par Evolution (phase 3) |

### Couche de compatibilité CMake

Les projets GNOME passent les `*_LDFLAGS` bruts de pkg-config
(`-L/dir -lglib-2.0`) à `target_link_libraries()`. Avec MSVC, CMake les
transmet tels quels à `link.exe`, qui les ignore (`LNK4044`), d'où des
symboles non résolus. `msvc-compat.cmake` redéfinit `target_link_libraries()`
pour traduire `-L<dir>` en `-LIBPATH:<dir>` et `-l<nom>` en `<nom>.lib`.
Il est chargé avec `-DCMAKE_PROJECT_INCLUDE=...` : aucun CMakeLists n'est
modifié pour cela.

## Phase 1 — evolution-data-server

### Modifications des sources EDS (branche `windows-native`)

Elles sont exportées dans `tools/patches/evolution-data-server.patch`.
Après chaque modification d'EDS, régénérer le patch depuis la racine du dépôt :
`git diff --relative=evolution-data-server <commit d'import> -- evolution-data-server > tools\patches\evolution-data-server.patch`
(`<commit d'import>` : commit d'import des sources amont, voir « Démarrage rapide »).

- `src/libedataserver/e-secret-store.c` : la mise en œuvre Win32 historique
  stockait les mots de passe encodés en base64 (donc en clair) dans
  `%LOCALAPPDATA%\evolution\secrets`. Elle est remplacée par le Gestionnaire
  d'identifiants Windows (cibles `Evolution-Data-Server:<uid>`, type
  `CRED_TYPE_GENERIC`, persistance `CRED_PERSIST_LOCAL_MACHINE`, protection
  DPAPI liée à l'utilisateur). Les secrets de session restent en mémoire.
  L'ancien fichier est lu en repli puis migré automatiquement.
- `src/libedataserver/e-ms-oapxbc.c` : libuuid remplacée par
  `g_uuid_string_random()` sous `G_OS_WIN32`.
- `CMakeLists.txt` : `uuid` et `libsecret-1` ne sont plus requis sous `WIN32`.

### Problèmes de configuration CMake (EDS, et les mêmes dans Evolution)

| Erreur | Cause | Correctif |
| --- | --- | --- |
| `zlib.h not found` | les tests `check_include_file`/`check_library_exists` ne voient pas le préfixe | `env.bat` ajoute le préfixe aux variables `INCLUDE` et `LIB` lues par `cl`/`link` |
| `zlib doesn't provide deflateEnd` | gvsbuild nomme la bibliothèque `zlib1.lib`, EDS cherche `z` | `build-gtk.bat` crée `z.lib` |
| `Failed to find icui18n…` | gvsbuild n'installe pas `icu-*.pc` ; sous Windows les bibliothèques s'appellent `icuin`/`icudt` | modèles `icu-uc.pc.in`, `icu-i18n.pc.in` |
| `sqlite3>=3.7.17 not found` (et `enchant-2`, `gsettings-desktop-schemas`, `libicalvcal`) | `.pc` absents | modèles correspondants, installés par `install-pc.bat` |
| `You need to install a working iconv` | `CMAKE_REQUIRED_LIBRARIES "-liconv"` passé tel quel à `link.exe` | nom nu `iconv` (CMake le traduit en `-liconv` ou `iconv.lib`), portable |
| `Invalid character escape '\U'` dans `CHECK_C_SOURCE_RUNS` | `file(TO_NATIVE_PATH)` produit des `\` insérés dans une chaîne C | chemins au format CMake (`/`), valides aussi en C sous Windows |
| Fenêtre « Debug Assertion Failed » pendant la configuration | les programmes de test sont compilés en Debug (CRT de débogage) ; `strftime("%lx%k")` déclenche une assertion modale | `CMAKE_TRY_COMPILE_CONFIGURATION=Release` dans `msvc-compat.cmake` |
| `Unable to find a way to determine timezone` | MSVC n'a ni `tm_gmtoff` ni la variable POSIX `timezone` | nouvelle détection `HAVE__TIMEZONE` ; `camel-mime-utils.c` et `e-time-utils.c` utilisent `_get_timezone()`/`_get_dstbias()` (gère aussi un décalage d'heure d'été ≠ 1 h) |
| `System doesn't support necessary interfaces for IPv6` | test écrit pour les en-têtes POSIX (`sys/socket.h`, `netdb.h`) | test avec `winsock2.h`/`ws2tcpip.h` + `ws2_32` sous Windows ; IPv6 reste activé |

### Problèmes de compilation et d'édition de liens (EDS)

Correctifs généraux dans `tools/cmake/msvc-compat.cmake` (valables aussi
pour Evolution) :

| Problème | Correctif |
| --- | --- |
| `gdbus-codegen`, `glib-mkenums`, `glib-genmarshal` « non reconnu » | ce sont des scripts Python sans extension : variables de cache préréglées à `python;<script>` (`find_program()` les conserve) |
| ICU 78 : `nested-namespace-definition exige /std:c++17` | `CMAKE_CXX_STANDARD 17` |
| Littéraux non ASCII mal lus (page de code ANSI) | `/utf-8` |
| `unistd.h`, `dirent.h`, `regex.h`, `utime.h`, `sys/time.h`, `sys/param.h` absents | en-têtes minimaux dans `tools/compat/include` : fonctions CRT sous leur nom POSIX, `dirent` sur `FindFirstFileW` (UTF-8), `regex` sur PCRE2-POSIX, `ftruncate` → `_chsize_s` |
| `mode_t`, `pid_t`, `ssize_t`, `S_IRWXU`, `S_ISDIR`, `O_ACCMODE`, `localtime_r`, `gmtime_r` | en-tête préliminaire `msvc-prelude.h` injecté partout (`/FI`) |
| C2011 « redéfinition de struct » (`winsock.h` contre `winsock2.h`) | `msvc-prelude.h` définit `_WINSOCKAPI_` : `windows.h` n'inclut plus l'ancien `winsock.h` |
| DLL sans bibliothèque d'import (`edbus-private.lib` introuvable) | `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON` (MSVC n'exporte rien par défaut, contrairement à ELF) |
| Plugins chargés mais « aucun code d'initialisation dans le module » | l'export automatique ne s'applique pas aux bibliothèques `MODULE` : `add_library(... MODULE ...)` est redirigé vers `SHARED` avec préfixe `lib` (noms identiques à Linux, attendus par les chargeurs) |

Correctifs dans les sources EDS (dans le patch) :

| Fichier | Problème | Correctif |
| --- | --- | --- |
| 61 fichiers de modules (EDS + Evolution) | C2375 : prototype de `e_module_load` sans `G_MODULE_EXPORT`, définition avec | `G_MODULE_EXPORT` ajouté au prototype (`tools/tools/fix-module-export-prototypes.py`, sans effet sous GCC) |
| `camel-enums.h`, `camel.h`, `camel-mime-utils.h`, `camel-provider.h`, `camel-debug.h` | variables globales partagées entre DLL (`camel_application_is_exiting`, …) : MSVC exige `__declspec(dllimport)` côté utilisateur | macro `CAMEL_VAR` ; `camel_verbose_debug` déclarée publiquement dans `camel-debug.h` et initialisée (un symbole *common* n'est pas exporté) |
| `camel-msgport.c` | `#ifdef G_OS_WIN32` avant l'inclusion de glib : `winsock2.h` jamais inclus | `#ifdef _WIN32` |
| `camel-mime-utils.c`, `e-data-server-util.c` | `getgid()`/`getppid()` (aléa du Message-ID / UID) | exclus sous Windows |
| `camel-gpg-context.c` | `local_error` non déclarée dans la branche Windows | déclarée (portage OpenPGP : phase 4c) |
| `e-book-sqlite.c` | macro variadique GNU `args...` | syntaxe C99 `...` / `__VA_ARGS__` |
| `gen-western-table.c` | entrée CRLF : `\r` dans les identifiants générés | suppression du `\r` final |
| `evolution-alarm-notify/CMakeLists.txt` (et `src/shell` d'Evolution) | icône compilée par `windres` (MinGW) | sous MSVC, le `.rc` est ajouté aux sources (compilé par `rc.exe`) |
| `e-data-server-util.c` | `system ("dbus-launch.exe")` hérité de MinGW (fenêtre console) | supprimé : GLib démarre son propre bus de session |

### Exécution

- `build-eds.bat` déplace les DLL d'EDS de `lib\` vers `bin\` (EDS les
  installe à la manière Unix ; Windows les cherche à côté de l'exécutable ou
  dans le PATH), compile les schémas GSettings (`glib-compile-schemas`) et
  copie les exécutables de `libexec\` dans `.\build\libexec\`.
- **Test réussi (2026-10-31)** : `libexec\evolution-source-registry.exe`
  démarre, le bus de session GLib (`gdbus.exe`) est lancé automatiquement,
  `list-sources.exe` liste les sources via D-Bus, et Camel charge ses cinq
  fournisseurs (IMAP, POP3, SMTP, local, NNTP), sans erreur ni avertissement.

### Dépendances supplémentaires à compiler (hors gvsbuild)

Sources dans `<workspace>\deps` (téléchargées par `tools\check-prereqs.bat -Install`) :

| Projet | Origine | Build |
| --- | --- | --- |
| NSS (3.133 beta) | `git clone --depth 1 https://github.com/nss-dev/nss.git` | `tools\build-nss.bat` (gyp-next + ninja, MSVC) |
| NSPR 4.38 | `https://ftp.mozilla.org/pub/nspr/releases/v4.38/src/nspr-4.38.tar.gz` | construit par `nss/build.sh` (autoconf, cl.exe) |
| libical 3.0.20 | `git clone --branch v3.0.20 https://github.com/libical/libical.git` | `tools\build-libical.bat` (CMake + Ninja) |

`gyp-next` est installé via `python -m pip install --user gyp-next`.

#### Problèmes rencontrés avec NSS/NSPR

1. **`Can't find vswhere on the path`** : `nss/build.sh` localise MSVC lui-même ;
   `C:\Program Files (x86)\Microsoft Visual Studio\Installer` est ajouté au PATH.
2. **`configure: error: Could not determine MSC version`** : le `cl.exe`
   français affiche `version<0xFF>19.44…` (espace insécable CP850). Le filtre
   sed de `nspr/configure` exigeait un espace ; il accepte désormais tout
   séparateur non numérique. Le correctif est appliqué automatiquement (et de
   façon idempotente) par `tools\tools\patch-nspr.py`, lancé depuis
   `build-nss.sh`. La locale
   est forcée à `C` (`LC_ALL=C`) car l'octet 0xFF n'est pas de l'UTF-8 valide.
   `VSLANG=1033` est sans effet si le pack de langue anglais n'est pas installé.
3. **MSYS2 se déclare `x86_64-pc-cygwin`** : NSPR doit voir une cible mingw
   pour choisir ses chemins WINNT/WIN95 avec cl.exe. `tools\nspr-config.site`
   fixe `ac_cv_build/host/target=x86_64-pc-mingw32` ; `CONFIG_SITE` est
   réexporté *dans* le shell bash car le profil MSYS2 le redéfinit
   (`tools\build-nss.sh`).
4. **`nsinstall: not found`** : outil fourni par MozillaBuild ;
   remplacé par un script minimal `tools\tools\nsinstall`.
5. **`link: unknown option -- n`** : le `/usr/bin/link` de coreutils (MSYS2)
   masquait `link.exe` de MSVC ; le dossier de `cl.exe` est placé en tête du PATH.
6. **`certdata.py` → `FileNotFoundError`** : la génération de `certdata.c`
   lance `perl` depuis Python (donc hors MSYS2). On utilise le Perl natif
   téléchargé par gvsbuild (`%GTK_BUILD%\tools\perl-5.20.0\x64\bin\perl.exe`),
   transmis par la variable `PERL` définie dans `env.bat`.

Résultat : `nspr4`, `plc4`, `plds4`, `nss3`, `nssutil3`, `smime3`, `ssl3`,
`softokn3`, `freebl3`, `nssckbi` (DLL) installées dans
`%GTK_BUILD%\gtk\x64\release\bin`. NSS nomme ses bibliothèques d'import
`nss3.dll.lib` : des copies `nss3.lib`, `nssutil3.lib`, `smime3.lib`,
`ssl3.lib` sont créées par `build-nss.bat` pour que `-lnss3` fonctionne. Les
fichiers `nss.pc`/`nspr.pc`, absents sous Windows, sont générés par
`build-nss.bat` à partir des modèles `tools\pkgconfig\*.pc.in`.

#### Problèmes rencontrés avec libical

1. **`LNK4044: option '/lglib-2.0' non reconnue`** : résolu par
   `msvc-compat.cmake` (voir « Couche de compatibilité CMake »).
2. **`error C2143` dans les en-têtes générés `i-cal-*.h`** : le dépôt avait
   été cloné avec `core.autocrlf=true`. Les templates du générateur étaient en
   CRLF et le générateur, qui écrit en mode texte, produisait `\` suivi de
   `\r\r\n` : les continuations de ligne des macros étaient cassées.
   Correctif : sources libical remises en LF (`git config core.autocrlf false`
   puis `git rm --cached -r . && git reset --hard`).
3. Windows n'ayant pas de base `zoneinfo`, libical est compilée avec
   `USE_BUILTIN_TZDATA=ON` (fuseaux installés dans `share\libical\zoneinfo`).

## Phase 2 — Evolution

Script : `tools\build-evolution.bat`. Fonctions optionnelles
désactivées (dépendances de bureau Linux) : gnome-desktop, canberra,
libnotify, gweather, autoar, ytnef, import PST, markdown (cmark),
coloration syntaxique (highlight), aide yelp, LDAP. Pour LDAP, Windows
fournit `wldap32` : piste pour les annuaires d'entreprise, plus tard.

Configuration :

| Problème | Correctif |
| --- | --- |
| `intltool-update not found` | `pacman -S intltool` dans MSYS2 (outil de build uniquement) ; scripts Perl lancés via le `perl.exe` de MSYS2 (variables de cache préréglées dans `msvc-compat.cmake`, `MSYS2_ROOT` défini dans `env.bat`) |
| `shared-mime-info>=0.22` | modèle `shared-mime-info.pc.in` (données seulement : sous Windows GLib utilise le registre pour les types de contenu) |
| `webkit2gtk-4.1>=2.40.0` | **bloquant attendu** → shim WebView2 (phase 3) |

Comme pour EDS, `TO_NATIVE_PATH`, le test `iconv` et le test de fuseau
horaire du `CMakeLists.txt` d'Evolution ont été corrigés de la même façon.

### Édition de liens et premier lancement

| Symbole non résolu | Cause | Correctif |
| --- | --- | --- |
| `camel_mime_special_table` | données `const` d'un fichier généré : ignorées par l'export automatique | `#pragma comment(linker, "/EXPORT:camel_mime_special_table,DATA")` dans `camel-mime-utils.c` (EDS) |
| `camel_header_param_encode_filenames_in_rfc_2047` | déclaré à la main (`extern`) dans `mail-config.c` | déclaration publique `CAMEL_VAR` dans `camel-mime-utils.h` ; l'`extern` local n'est gardé que si `CAMEL_VAR` n'existe pas |
| `is_any_gettext_catalog_installed` | définie dans `main.c` (exécutable) mais appelée par la DLL du shell | déplacée dans `e-shell.c` |
| `_e_get_data_server_icondir` | l'en-tête déclarait un nom différent de celui généré par `GETTER(e_data_server_icondir)` | en-tête aligné : `_e_get_e_data_server_icondir` |
| `context` (dossiers virtuels) | variable globale de `libemail-engine` utilisée par `evolution-mail` | accesseur `mail_vfolder_get_context ()` |

`build-evolution.bat` déplace les DLL privées d'Evolution (`lib\*.dll`) dans
`bin\`, compile les schémas GSettings et copie `evolution.exe` ainsi que ses
DLL dans `.\build\`. La configuration est stockée par GLib dans le registre
Windows (`HKCU\Software\GSettings`).

**Premier lancement réussi (2026-10-31)** — après démarrage manuel de
`libexec\evolution-source-registry.exe` : fenêtre principale « Courriel »,
dossiers locaux, interface en français, sans erreur (seul avertissement
attendu : pas de fournisseur `sendmail` sous Windows). Capture :
`tools/screenshots/2026-10-31-first-run.png`.

### Phase 2b — démarrage automatique des services EDS (fait)

Le bus de session de GLib sous Windows n'active pas les services à partir
de fichiers `.service`. Nouvelle fonction publique (Windows uniquement)
`e_util_win32_ensure_dbus_service (bus_name, cancellable, error)` dans
`libedataserver` :

1. vérifie avec `NameHasOwner` si le nom D-Bus a un propriétaire ;
2. sinon lance l'exécutable correspondant depuis `libexec\` (chemin
   relocalisé, `E_DATA_SERVER_LIBEXECDIR`) avec `CreateProcessW` et
   `CREATE_NO_WINDOW` (pas de console) ;
3. attend (30 s maximum) que le service possède son nom ; ne fait rien si
   elle est appelée par le service lui-même.

Correspondances : `Sources5` → `evolution-source-registry.exe`,
`AddressBook10` → `evolution-addressbook-factory.exe`, `Calendar8` →
`evolution-calendar-factory.exe`, `UserPrompter0` →
`evolution-user-prompter.exe`. Appelée juste avant la création des proxys
D-Bus dans `e-source-registry.c`, `e-book-client.c`, `e-cal-client.c` et
`e-user-prompter.c` (déjà exécutés dans des threads).

La source prédéfinie « Sendmail » n'est plus chargée sous Windows
(`e-source-registry-server.c`) : pas de sendmail ni de fournisseur Camel.

**Validé (2026-10-31)** : lancer uniquement `evolution.exe` démarre le bus,
le registre puis la fabrique carnet d'adresses, sans fenêtre console ; un
compte IMAP réel configuré par l'utilisateur synchronise ses dossiers et
affiche la liste des messages. L'affichage du contenu HTML attend la phase 3.

Les services héritent du `PATH` du client pour trouver leurs DLL. Pour
EvolutionWin, `evo-core` place lui-même le dossier de `evo-core.dll` (`bin\`)
en tête du `PATH` au démarrage (phase 5) : aucun réglage système requis.

## Phase 3 — préparation : shim WebKit sur WebView2

### Stratégie

`EWebView` hérite de `WebKitWebView` et Evolution utilise l'API WebKit à
~1500 endroits. Plutôt que de réécrire ces appels, on fournit une
**bibliothèque de compatibilité** (`webkit2gtk-4.1` côté pkg-config, en-têtes
`webkit2/webkit2.h`) qui implémente sur WebView2 uniquement le sous-ensemble
d'API réellement utilisé : environ 110 fonctions/types WebKit et JSC.
L'inventaire exact (nombre d'occurrences par symbole) est dans
`tools/webkit-api-usage.txt`.

Correspondances principales :

| WebKitGTK | WebView2 |
| --- | --- |
| `webkit_web_view_load_html/uri/bytes` | `NavigateToString` / `Navigate` |
| `webkit_web_view_evaluate_javascript` + `JSCValue` | `ExecuteScript` (résultat JSON → `JSCValue` émulé) |
| `webkit_user_content_manager_register_script_message_handler` | `window.chrome.webview.postMessage` + `WebMessageReceived` |
| `webkit_web_context_register_uri_scheme` (`evo-http`, `cid`, `mail`, …) | `AddWebResourceRequestedFilter` + `WebResourceRequested` |
| `decide-policy` | `NavigationStarting` / `NewWindowRequested` |
| `WebKitFindController` | `window.find()` via script |
| `WebKitPrintOperation` | `ICoreWebView2_16::Print*` |

### Étape 1 réalisée : squelette du shim (`tools/webkit-shim`)

- Une seule DLL `webkit2gtk-4.1.dll` fournit l'API WebKitGTK 4.1 et l'API
  JavaScriptCore-GLib ; fichiers `webkit2gtk-4.1.pc` et
  `javascriptcoregtk-4.1.pc` (version 2.44.0). En-têtes dans
  `include\webkit2gtk-4.1\{webkit2,jsc}`. Script : `tools\build-webkit-shim.bat`.
- `WebKitWebView` dérive de `GtkContainer` ; sa structure de classe est
  publique (`EWebView` et `EWebKitEditor` l'embarquent). Les signaux et
  propriétés qu'Evolution utilise sont enregistrés avec leurs vraies
  signatures (`load-changed`, `decide-policy`, `context-menu`,
  `script-message-received::*`, `initialize-web-extensions`, `found-text`,
  `finished`, propriétés `title`, `editable`, `zoom-level`, 60 propriétés de
  `WebKitSettings`…).
- `JSCValue` repose sur un nœud json-glib (WebView2 renvoie du JSON).
- Implémentation pour l'instant **vide** : chaque point à réaliser est marqué
  `/* TODO WebView2 */` ; les appels asynchrones échouent avec
  `G_IO_ERROR_NOT_SUPPORTED`.
- Evolution : `src/web-extensions`, `src/modules/webkit-editor/web-extension`
  et `src/modules/webkit-inspector` ne sont pas compilés sous Windows (pas
  d'extension dans le processus de rendu avec WebView2).
- Correctif `cmake/modules/GLibTools.cmake` (Evolution) : un chemin absolu
  servait de nom de variable (`${C:/…/x.gresource_DEPS}`), erreur de syntaxe
  CMake sous Windows ; remplacé par un nom fixe.

### Étape 2 réalisée : rendu WebView2 (2026-10-31)

`tools/webkit-shim/src/webview2-host.{h,cpp}` (C++/WRL, API C) :
environnement WebView2 partagé (dossier utilisateur
`%LOCALAPPDATA%\evolution\webview2` via le cache GLib, chargeur statique
`WebView2LoaderStatic.lib`), un contrôleur par vue dans une fenêtre native
enfant (`gdk_window_ensure_native`), file des commandes en attente pendant la
création asynchrone.

| WebKitGTK | Implémentation |
| --- | --- |
| `load_html` / `load_bytes` / `load_plain_text` | contenu servi par le schéma interne `webkit-shim-page:` (pas de limite de 2 Mo, contrairement à `NavigateToString`) |
| schémas d'Evolution (`mail`, `cid`, `evo-file`, `evo-http(s)`, `gtk-stock`) | déclarés à la création de l'environnement (`CustomSchemeRegistration`), requêtes via `WebResourceRequested` → gestionnaires `webkit_web_context_register_uri_scheme()`, réponse lue de façon asynchrone puis `CreateWebResourceResponse` |
| sous-ressources `http(s)` des pages Evolution | réécrites en `evo-http(s)` comme le faisait l'extension web (politique de contenu distant d'Evolution) |
| `evaluate_javascript` | `ExecuteScript` d'un `eval` global enveloppé : exceptions → `WEBKIT_JAVASCRIPT_ERROR_SCRIPT_FAILED`, valeur JSON → `JSCValue` |
| `window.webkit.messageHandlers.<nom>.postMessage` | défini par un « script de page » injecté dans chaque document et frame, relayé par `chrome.webview.postMessage` (événement du frame inclus) |
| feuilles de style utilisateur | `<style id="webkit-shim-user-styles">` mis à jour à chaque changement |
| `load-changed`, `decide-policy`, `context-menu`, `mouse-target-changed` | `NavigationStarting`/`ContentLoading`/`NavigationCompleted`, `NewWindowRequested`, `ContextMenuRequested` (événement GDK synthétisé), `StatusBarTextChanged` |

Côté Evolution (`e-web-view.c`, Windows) : `e-convert.js`, `e-web-view.js` et
les greffons `preview-plugins` sont ajoutés comme script utilisateur du cadre
principal (remplace l'extension web) ; `Evo.getUriTooltip` et
`EvoConvert.linkRequiresReference` sont approximés en JavaScript.

Problèmes rencontrés :

1. **« Ã€ » au lieu de « À »** : texte servi sans `charset` lu en
   Windows-1252 → `charset=utf-8` ajouté aux types `text/*`.
2. **Feuille de style ignorée** : GIO sous Windows donne des types de
   contenu sous forme d'extension (`.css`) ; WebView2 refuse une CSS sans le
   bon type MIME → conversion `g_content_type_get_mime_type()` (+ table de
   secours).
3. **Corps du message invisible (iframe de hauteur nulle)** : le script de
   la page principale (`webkit-shim-page:`) lit le contenu des iframes
   `mail:` ; origines différentes pour Chromium. WebKitGTK traite ces schémas
   locaux comme fiables : l'environnement WebView2 est lancé avec
   `--disable-web-security --disable-site-isolation-trials`. Compromis
   assumé pour la version GTK transitoire (le contenu distant reste filtré
   par Evolution) ; la nouvelle interface WinUI servira tout sous une seule
   origine.

Diagnostic : `WEBKIT_SHIM_DEBUG=1` trace navigations, requêtes, réponses,
scripts et erreurs JavaScript des pages.

**Validé** : messages texte et HTML (mise en page, polices web, images
locales) affichés dans le panneau d'aperçu, avec un compte IMAP réel.

Limites connues (version transitoire, non prioritaires) : erreurs
« Evo is not defined » sur le document vide initial (sans effet),
raccourcis clavier d'Evolution inactifs quand le focus est dans la page,
recherche dans la page, impression, éditeur de composition (phase 4 non
poursuivie pour GTK).

### Autres portages côté Evolution

- `src/e-util/e-passwords.c` (ancienne API de mots de passe) : sous Windows,
  `e_secret_store_*()` d'EDS (Gestionnaire d'identifiants) au lieu de
  libsecret ; libsecret n'est plus requise sous `WIN32` (Evolution, et
  `libedataserver-1.2.pc` d'EDS).
- Filtres antispam bogofilter/spamassassin désactivés (pilotage de programmes
  externes par `fork()`/`waitpid()`) : à porter.
- `certificate-manager.c` : tableau de taille variable (non supporté par
  MSVC) remplacé par `g_newa`. `e-shell.c` : `windows.h` manquant.
- `msvc-compat.cmake` : `-lm`/`-ldl` ignorés (fournis par le CRT).

### Extension web (processus WebKit)

`src/web-extensions/e-web-extension.c` ne fait que :

1. charger `e-convert.js`, `e-web-view.js` puis les plugins JS (données dans
   `data/webkit`) à chaque création du contexte JS ;
2. exposer deux fonctions natives synchrones (`Evo.getUriTooltip`,
   `EvoConvert.linkRequiresReference`) ;
3. réécrire les requêtes `http(s)` en `evo-http(s)` pour les faire passer
   par Evolution.

Sous WebView2 : (1) `AddScriptToExecuteOnDocumentCreated`, (2)
réimplémentation en JavaScript, (3) `WebResourceRequested`. Aucun code natif
dans le processus de rendu n'est nécessaire.

SDK : `Microsoft.Web.WebView2` 1.0.4314.35 (NuGet) dans
`<workspace>\deps\webview2\sdk`. Runtime Edge WebView2 156 déjà
présent sur Windows 11.

## Phase 4b — Exchange Online / Microsoft 365 (objectif : remplacer Outlook)

### Protocole : Microsoft Graph, pas EWS

Microsoft retire EWS d'Exchange Online : blocage automatique des tenants non
configurés depuis le **1er octobre 2026**, suppression définitive le
**1er avril 2027**, sans exception. Seul Exchange Server sur site garde EWS.
La cible est donc l'API **Microsoft Graph**.

Le module **evolution-ews** (dépôt séparé,
`https://gitlab.gnome.org/GNOME/evolution-ews.git`) fournit deux types de
comptes :

| Type de compte | Protocole | Usage dans ce portage |
| --- | --- | --- |
| « Microsoft 365 » | Microsoft Graph | **Exchange Online** (courrier, agenda, contacts) |
| « Exchange Web Services » | EWS | Exchange sur site uniquement |

Limites connues de Graph par rapport à EWS : pas d'accès aux dossiers
publics, couverture partielle des boîtes d'archive.

### Authentification OAuth2 (Entra ID)

L'authentification de base est désactivée sur Exchange Online : la connexion
passe par OAuth2. Points à traiter :

1. **Fenêtre de connexion** : sous Linux, elle utilise WebKitGTK. Sous
   Windows, elle passera par WebView2, ce qui supporte aussi
   l'authentification moderne, MFA, accès conditionnel. Variante possible :
   navigateur système + redirection loopback (RFC 8252).
2. **Inscription d'application Entra ID** : evolution-ews fournit un
   identifiant client par défaut, mais beaucoup de tenants exigent un
   consentement administrateur. Pour un déploiement en entreprise, prévoir
   une inscription d'application dans le tenant, avec permissions déléguées
   `Mail.ReadWrite`, `Mail.Send`, `Calendars.ReadWrite`,
   `Contacts.ReadWrite`, `User.Read`, `offline_access`. L'identifiant client
   et le tenant seront préconfigurables à l'installation (propriétés MSI,
   phase 5), pour un déploiement sans saisie utilisateur.
3. **Jetons** : stockés dans le Gestionnaire d'identifiants Windows (même
   mécanisme que les mots de passe, voir phase 1).

### OpenPGP (GPG)

`camel-gpg-context.c` lance `gpg` avec `fork()`/`pipe()`/`fcntl()` ; la
branche Windows n'était qu'un `FIXME: Port me`. Elle est désormais portée sur
`g_spawn_async_with_pipes()` avec Gpg4win (`gpg.exe`) : voir le huitième
jalon du client natif. S/MIME, qui passe par NSS, n'est pas concerné.

### Étapes

- [x] Compiler evolution-ews avec MSVC (2026-10-31)
- [x] Fenêtre OAuth2 sur WebView2 dans EvolutionWin (2026-10-31)
- [ ] Test de bout en bout sur un tenant Exchange Online : courrier, agenda, contacts

### Compilation d'evolution-ews

Sources dans `<workspace>\evolution-ews` (dépôt GNOME, commit amont
`19cb82b3`, branche `windows-native`). Script : `tools\build-ews.bat`
(après `build-eds.bat`), avec `WITH_EVOLUTION=OFF` : seules les parties EDS
sont compilées (fournisseurs Camel `ews` et `microsoft365`, backends carnet
d'adresses et agenda, modules du registre, bibliothèques privées
`evolution-ews.dll`, `evolution-microsoft365.dll`, `camel*-priv.dll`,
`evolution-ews-common.dll`, déplacées dans `bin\`). Les pages de
configuration de l'interface GTK ne servent pas à EvolutionWin.
`WITH_MSPACK=OFF` : pas de libmspack (décompression du carnet d'adresses
hors ligne OAB d'EWS).

Correctifs : prototypes `G_MODULE_EXPORT` (C2375) par
`tools\tools\fix-module-export-prototypes.py`, dont le motif confondait
`x = fonction (...);` avec un prototype (corrigé : pas de `=` avant le nom) ;
`e-cal-backend-ews-utils.h` déclare avec `G_MODULE_EXPORT` les trois
fonctions exportées pour les tests.

### Comptes Microsoft 365 dans EvolutionWin

Le connecteur Microsoft 365 obtient ses jetons d'accès par
`e_source_get_oauth2_access_token_sync` : le registre EDS les renouvelle à
partir du jeton de rafraîchissement stocké (`OAuth2::Microsoft365[<adresse>]`
dans le magasin de secrets d'EDS, donc le Gestionnaire d'identifiants
Windows). Seule la **connexion initiale** est interactive ; sous Linux elle
passe par un dialogue GTK/WebKit d'EDS, remplacé ici par EvolutionWin.

evo-core :

- charge au démarrage, avant le registre, les modules OAuth2
  (`module-microsoft365-backend`, `module-ews-backend`,
  `module-oauth2-services`) : `ESourceRegistry` y trouve les services ;
- `evo_core_add_microsoft365_account` : sources créées comme par l'assistant
  d'Evolution, une collection (backend `microsoft365`, authentification
  `Microsoft365`, réglages `CamelM365Settings`) portant compte de courrier,
  identité et serveur d'envoi ; le module du registre y ajoute les agendas
  et carnets d'adresses (Graph). Identifiant d'application et tenant
  facultatifs (application d'Evolution `20460e5d-…` et tenant `common` par
  défaut) ;
- `evo_core_oauth2_begin` / `evo_core_oauth2_finish` : même construction que
  le dialogue d'EDS (paramètres du service, `state` vérifié au retour, refus
  `error_description` remonté), code échangé et jetons enregistrés par
  `e_oauth2_service_receive_and_store_token_sync` ;
- événement `oauth2-required` quand le connecteur refuse l'authentification
  (jeton absent ou révoqué ; pas sur une erreur réseau) ;
- suppression : la collection entière (agendas et carnets compris) et le
  jeton enregistré.

EvolutionWin : choix « Microsoft 365 / Exchange Online » dans l'assistant
d'ajout de compte (sans serveurs ni mot de passe), puis fenêtre de connexion
WebView2 ; la navigation vers l'adresse de redirection
(`…/oauth2/nativeclient`) est interceptée et transmise au moteur. Même
fenêtre sur `oauth2-required`.

**Testé** (2026-10-31) avec un compte fictif, créé puis supprimé dans le
registre réel : création, adresse de connexion (point d'accès v2.0, droits
Graph courrier/agenda/contacts/tâches, `offline_access`), refus de
l'utilisateur, réponse falsifiée (`state` différent) rejetée, suppression
sans reste. **Reste à valider** avec un vrai compte Microsoft 365 :
connexion, relève, envoi, agendas et contacts ; un tenant d'entreprise peut
exiger le consentement d'un administrateur pour l'application d'Evolution
ou une inscription propre (identifiant et tenant dans l'assistant).

Non fait : comptes EWS (Exchange sur site, `ews`) dans l'assistant ; les
fournisseurs et backends sont compilés et installés.

## Phase 5 — Installeur MSI

Objectif : un paquet `.msi` autonome, déployable silencieusement sur un poste
(`msiexec /i Evolution-x64.msi /qn`), par GPO, Intune ou SCCM, contenant
EvolutionWin, EDS et **toutes** leurs dépendances. Rien ne doit être requis sur
le poste en dehors de Windows 11.

### Réalisation (2026-10-31)

```bat
tools\build-msi.bat                    :: recompile EvolutionWin dans l'arborescence, puis le MSI
tools\build-msi.bat -Version 1.0.0     :: version forcée (défaut : fichier VERSION, ou EVO_MSI_VERSION)
tools\build-msi.bat -SkipAppBuild      :: reprend .\build\ (sortie de build-winui-app.bat)
```

Résultat : `dist\Evolution-<version>-x64.msi` (≈ 42 Mo, 1 199 fichiers,
149 Mo installés). Fichiers : `tools\build-msi.bat` (environnement),
`tools\build-msi.ps1` (arborescence, dépendances, signature, WiX),
`tools\msi\Evolution.wxs` (source WiX).

Arborescence installée (`%ProgramFiles%\Evolution`), au format d'un préfixe :
EDS et Camel calculent leurs chemins depuis leurs DLL, GLib trouve ses modules
GIO et ses schémas depuis la sienne, libical ses fuseaux depuis l'exécutable.

| Dossier | Contenu |
| --- | --- |
| `bin\` | `EvolutionWin.exe` et le Windows App SDK autonome, `evo-core.dll`, 43 DLL du préfixe (imports PE), NSS chargé dynamiquement (`softokn3`, `freebl3`, `nssckbi`), `gdbus.exe`, `gspawn-win64-helper*.exe`, runtime Visual C++ (copie locale) |
| `libexec\` | `evolution-source-registry`, fabriques carnet d'adresses et agenda (et leurs sous-processus), `camel-index-control`, `camel-gpg-photo-saver` |
| `lib\evolution-data-server\` | fournisseurs Camel, backends carnet/agenda, modules du registre |
| `lib\gio\modules\` | `gioopenssl` (TLS : IMAPS/SMTPS), `giognomeproxy` |
| `share\` | `gschemas.compiled`, fuseaux libical, traductions (EDS, GLib, glib-networking, libsoup) |

Choix et problèmes rencontrés :

1. **GTK exclu** (65 → 43 DLL, 47 → 42 Mo) : seuls `evolution-user-prompter`
   et `module-trust-prompt` (dialogues GTK d'EDS) et le fournisseur RSS
   (greffon d'Evolution) l'importaient. EvolutionWin demande lui-même mots de
   passe et certificats ; limite : une confirmation demandée par un backend
   carnet/agenda en ligne (CalDAV, CardDAV) échoue au lieu d'afficher un
   dialogue. La version GTK transitoire (`evolution.exe`) n'est pas dans le MSI.
2. **Services EDS sans DLL à côté** : `evo-core` ajoute son dossier en tête du
   `PATH` au démarrage ; les services lancés depuis `libexec\` en héritent.
3. **Validation ICE** (`wix msi validate`, exécutée par le script) :
   - ICE03 : les `.mui` du Windows App SDK portent des langues récentes
     (1152, 1153, 1169) inconnues des tables ICE ; sans effet, ignorée ;
   - ICE43/ICE57 : raccourci du menu Démarrer d'un paquet par machine
     (`ALLUSERS=1`) supposé par utilisateur ; ignorées. Le raccourci est porté
     par le fichier `EvolutionWin.exe` (supprimé avec lui) ;
   - ICE61 : réinstallation à version égale autorisée (compilations de test).
4. **Condition de lancement** : runtime WebView2 présent (clé EdgeUpdate,
   version différente de `0.0.0.0`).
5. **Mise à jour pendant qu'Evolution tourne** (0.9.1 sur 0.9.0, VM) : le
   dialogue « fichiers en cours d'utilisation » restait caché derrière
   l'Explorateur, et le MSI relancé échouait avec l'erreur 1618 (« une autre
   installation est en cours »). Depuis la 0.9.2, `util:CloseApplication`
   (extension `WixToolset.Util.wixext`) ferme EvolutionWin (message de
   fermeture, puis arrêt après 5 s) et arrête les services EDS et `gdbus.exe`
   avant le remplacement des fichiers. Sur un poste de développement, cela
   arrête aussi les services EDS lancés depuis le préfixe (même nom de
   processus) ; EvolutionWin les relance au besoin.

**Validé** : extraction du MSI (`msiexec /a`) et exécution depuis
l'arborescence extraite, avec un `PATH` réduit au système : bus D-Bus et
registre EDS démarrés depuis le paquet, connexion IMAPS, compte, dossiers et
messages de l'utilisateur listés ; `EvolutionWin.exe` ouvre sa fenêtre et
démarre les fabriques carnet d'adresses et agenda du paquet. L'installation
réelle (`msiexec /i`, élévation UAC) reste à faire par l'utilisateur.

Reste à faire : icône de l'application, enregistrement comme client de
messagerie et associations `.eml`/`.ics`/`.vcf` (quand EvolutionWin saura
ouvrir un fichier ou une URL `mailto:` passés en argument), choix des langues,
Gpg4win en prérequis optionnel.

### Outil

**WiX Toolset v5** (outil de référence pour les MSI, sources XML
versionnables, construction en ligne de commande) :
`dotnet tool install --global wix --version 5.0.2`, vérifié et installé par
`check-prereqs.ps1` (prérequis facultatif).

### Contenu du paquet

| Élément | Origine | Remarque |
| --- | --- | --- |
| `EvolutionWin.exe`, `evo-core.dll`, `evolution.exe`, modules, plugins | `./build` | |
| Services EDS (`evolution-source-registry`, fabriques carnet/agenda, `evolution-alarm-notify`) | build EDS | démarrés par EDS (pas d'activation D-Bus sous Windows) |
| DLL des dépendances (GLib, GTK3, libsoup, NSS, libical…) | préfixe `%GTK_BUILD%\gtk\x64\release\bin` | **uniquement celles réellement importées**, déterminées en parcourant les imports PE (`dumpbin /dependents`) à partir des exécutables et modules, pour ne pas embarquer tout le préfixe |
| Modules GIO (`glib-networking` : TLS pour IMAPS/SMTPS) | `lib\gio\modules` | indispensable pour les connexions chiffrées ; cache `giomodule.cache` régénéré au build |
| Chargeurs gdk-pixbuf (PNG, SVG via librsvg…) | `lib\gdk-pixbuf-2.0` | `loaders.cache` généré au build avec chemins relatifs |
| Schémas GSettings | `share\glib-2.0\schemas` | `gschemas.compiled` précompilé au build |
| Thèmes d'icônes Adwaita/hicolor, données Evolution (`share\evolution`), fuseaux libical | `share\…` | |
| Traductions (`share\locale`) | builds | option « langues » dans l'installeur |
| Runtime Visual C++ | Visual Studio (`VC\Redist\MSVC`) | copie *app-local* de `vcruntime140*.dll`/`msvcp140.dll` dans le dossier d'installation : aucun prérequis système |
| Runtime WebView2 | Windows 11 l'inclut | vérification au lancement de l'installeur (condition MSI) ; en option, bundle WiX/Burn avec le bootstrapper Evergreen pour les postes qui ne l'ont pas |

Toute l'application est relocalisable : les chemins de données sont calculés
au démarrage relativement à l'exécutable (`e-util-private.h`,
`e_util_win32_initialize()` côté EDS), donc l'installation peut se faire dans
n'importe quel dossier.

### Intégration Windows

- Installation par machine dans `%ProgramFiles%\Evolution`, raccourci menu Démarrer.
- Enregistrement comme client de messagerie (protocole `mailto:`, « Applications
  par défaut »), associations `.eml`, `.ics`, `.vcf`.
- `UpgradeCode` fixe → mises à jour majeures propres (`MajorUpgrade`),
  désinstallation complète par « Applications installées ».
- Données utilisateur dans `%LOCALAPPDATA%\evolution` (non supprimées à la
  désinstallation).
- Signature Authenticode du MSI et des binaires (`signtool`) : **prévue, faite
  plus tard** (décision du 2026-10-31). `build-msi.bat` aura une étape de
  signature optionnelle, activée seulement si un certificat est fourni
  (variables `SIGN_CERT` / `SIGN_TIMESTAMP_URL`), et produira sinon un MSI non
  signé utilisable en test.

## Phase 6 — Apparence et comportement Windows natifs (demandée le 2026-10-31)

Objectif : qu'Evolution ressemble à une application Windows et non à un
portage de GNOME (fenêtres, boutons, listes, menus, dialogues).

Constat technique : l'interface d'Evolution est écrite en GTK3 (des
centaines de milliers de lignes : fenêtres, vues de listes, éditeurs,
préférences). GTK dessine lui-même ses widgets ; ce ne sont pas des
contrôles Win32. Deux niveaux sont donc possibles :

### 6a — Intégration native de l'interface GTK existante (abandonnée)

| Élément | Moyen |
| --- | --- |
| Boutons, listes, onglets, barres de défilement, cases | thème GTK **`win32`** intégré à GTK3 : dessin par le moteur de thèmes de Windows (uxtheme), donc rendu identique aux contrôles natifs |
| Barre de titre, bordures, accrochage (Snap), ombres | décorations **natives** de Windows au lieu des décorations GNOME dessinées par GTK (`GTK_CSD=0`, pas de *header bar* sous Windows) |
| Police | Segoe UI (police système), tailles de Windows |
| Ouvrir/Enregistrer un fichier | `GtkFileChooserNative` → dialogue Windows `IFileDialog` |
| Impression | dialogue d'impression Windows (GTK utilise déjà `PrintDlgEx` sous Windows) + impression WebView2 |
| Notifications | notifications Windows (toasts) via `GNotification` (backend Windows de GLib) |
| Icônes | jeu d'icônes Fluent / Segoe Fluent Icons au lieu d'Adwaita |
| Mode sombre, couleur d'accentuation | lecture des préférences Windows (registre `AppsUseLightTheme`, couleur d'accent) |
| Barre des tâches | badge de messages non lus (`ITaskbarList3`), liste de raccourcis (*jump list* : Nouveau message, Nouveau rendez-vous) |
| Client par défaut | `mailto:`, `.eml`, `.ics`, `.vcf` dans « Applications par défaut » (prévu au MSI) |

### 6b — Interface réellement native (contrôles Windows)

Utiliser les contrôles Windows eux-mêmes (WinUI 3 / Windows App SDK, ou
Win32/ComCtl) impose de **réécrire toute l'interface** d'Evolution : seuls
EDS (comptes, courrier via Camel, agenda, contacts), WebView2 et la logique
non graphique seraient réutilisés. C'est un nouveau client de messagerie
bâti sur le moteur d'Evolution.

**Décision de l'utilisateur (2026-10-31) : interface réécrite avec de vrais
contrôles Windows (6b).** Le moteur conservé : EDS (comptes, sources,
carnets d'adresses, agendas, services), Camel (IMAP, SMTP, POP3, local, et
plus tard Microsoft 365 via evolution-ews), le Gestionnaire d'identifiants,
NSS/S-MIME. L'interface GTK d'Evolution n'est plus la cible finale.

Choix complémentaires (2026-10-31) :

- **Technologie : WinUI 3 (Windows App SDK) en C++/WinRT**, avec Visual
  Studio 2022. Le C++ appelle directement les bibliothèques C d'EDS/Camel
  dans le processus ; contrôles natifs (`NavigationView`, `TreeView`, `ListView`,
  `CalendarView`…), Fluent, mode sombre, WebView2 intégré.
- **Evolution GTK = version transitoire** : on termine seulement l'affichage
  des messages dans WebView2 (phase 3, étape 2) pour disposer d'un client
  utilisable pendant la réécriture, puis la version GTK n'évolue plus. La
  phase 6a est abandonnée.

## Phase 6b — Client natif WinUI 3 (dossier `winui/`)

### Architecture

```text
winui/
  core/   evo-core.dll  (C, GLib) : EDS + Camel, thread GLib dédié, API C + JSON
  app/    EvolutionWin.exe (C++/WinRT, WinUI 3) : interface native, WebView2
```

- **`evo-core`** (C, compilé par CMake comme EDS) possède un **thread GLib**
  qui exécute le contexte principal par défaut : le registre des sources
  (`ESourceRegistry`), une `CamelSession` et tous les objets EDS/Camel n'y
  vivent que là. L'interface lui adresse des requêtes (comptes, dossiers,
  messages, contenu, agenda, contacts) ; les réponses reviennent par rappel,
  sérialisées en **JSON** (frontière simple et stable : aucun type GLib côté C++).
- **`EvolutionWin`** (C++/WinRT, Windows App SDK 1.7, non empaqueté,
  autonome : `WindowsAppSDKSelfContained`) reçoit les rappels sur le thread
  GLib et les renvoie sur le thread de l'interface par le `DispatcherQueue`.
  JSON lu avec `Windows.Data.Json`.
- Interface : `NavigationView` (Courrier, Calendrier, Contacts, Paramètres) ;
  courrier en trois volets : `TreeView` des dossiers, `ListView` des messages,
  contrôle `WebView2` pour la lecture.

Versions : Microsoft.WindowsAppSDK 1.7.260224002 (dernier paquet monolithique
avant le découpage de la 1.8), Microsoft.Windows.CppWinRT 2.0.250303.1,
Microsoft.Windows.SDK.BuildTools 10.0.26100.9457, Microsoft.Web.WebView2,
Microsoft.Windows.ImplementationLibrary. Restaurés par `nuget.exe`
(`tools\tools\nuget.exe`) ; compilés par MSBuild. Les modèles de
projet C++ WinUI ne sont pas installés dans Visual Studio : le projet est
écrit à la main.

### Premier jalon — atteint le 2026-10-31

Fenêtre native, comptes et dossiers depuis EDS/Camel, liste des messages,
lecture d'un message dans WebView2.

Compilation (après `build-eds.bat`) :

```bat
tools\build-winui-core.bat   :: evo-core.dll + evo-core-test.exe -> .\build\
tools\build-winui-app.bat    :: nuget restore + MSBuild -> .\build\EvolutionWin.exe
tools\run-winui.bat          :: lance .\build\EvolutionWin.exe avec le préfixe dans PATH
```

Exécution (développement) : ajouter `<workspace>\gtk-build\gtk\x64\release\bin`
au `PATH` (evo-core, EDS, GLib), puis lancer `.\build\EvolutionWin.exe`
(ou utiliser `tools\run-winui.bat`). `evo-core-test.exe` vérifie le moteur
seul en console (lecture seule : `EVO_CORE_READ_ONLY=1`, aucun message n'est
marqué comme lu).

#### evo-core (`winui/core`)

- `EvoCoreSession` (sous-classe de `CamelSession`) : chaque service est
  configuré depuis sa source EDS (`e_source_camel_configure_service`), le
  nom affiché en vient aussi ; authentification avec le mot de passe
  enregistré par EDS (`e_source_credentials_provider_lookup_sync`, donc le
  Gestionnaire d'identifiants Windows), sans interface ; filtre de réception
  vide (pas encore de règles) ; certificats invalides refusés (dialogue à
  venir).
- Dossiers de données propres (`…\evolution\evolution-windows\mail`) : la
  version GTK peut tourner en même temps sans partager ses caches.
- Les comptes IMAP sont des « magasins hors ligne » : ils doivent être mis
  en ligne (`camel_offline_store_set_online_sync`) avant toute opération,
  sinon « Vous devez travailler en ligne ».
- Comptes ignorés : dossiers de recherche (`vfolder`, règles propres à
  Evolution) et flux RSS (greffon d'Evolution).
- Rendu d'un message : partie HTML préférée, images `cid:` intégrées en
  `data:`, texte brut converti (fond blanc).

#### EvolutionWin (`winui/app`)

- Projet MSBuild écrit à la main (les modèles C++ WinUI ne sont pas
  installés) : Windows App SDK 1.7 **non empaqueté et autonome**
  (`WindowsPackageType=None`, `WindowsAppSDKSelfContained=true`) : aucune
  installation du runtime Windows App SDK n'est nécessaire.
- `CoreBridge.h` : rappels d'evo-core → `DispatcherQueue` du thread de
  l'interface (jeton de vie partagé : aucun rappel après la fermeture).
- `FolderItem` (classe Windows Runtime `[bindable]`) : contenu des nœuds du
  `TreeView` affiché par un `DataTemplate` (mettre des éléments d'interface
  dans les nœuds afficherait leur nom de type et casse le recyclage).
- Lecteur WebView2 : scripts désactivés, contenu distant bloqué
  (`WebResourceRequested` → 403), liens ouverts dans le navigateur par
  défaut, dossier de données `%LOCALAPPDATA%\EvolutionWin\WebView2`
  (`WEBVIEW2_USER_DATA_FOLDER`, le dossier de l'exécutable n'étant pas
  inscriptible sous Program Files), fond blanc.

Validé avec le compte IMAP réel de l'utilisateur : arborescence avec
compteurs de non-lus, liste des messages (non lus en gras), lecture des
messages texte et HTML.

### Deuxième jalon — rafraîchissement, rédaction et envoi (2026-10-31)

evo-core :

- **Dossiers suivis** : les dossiers ouverts restent en cache ; leur signal
  `CamelFolder::changed` (émis dans le thread du moteur) devient l'événement
  `folder-changed` (non-lus, total, ajoutés/supprimés/modifiés).
- **Relève** : immédiatement après le démarrage, puis toutes les 5 minutes,
  ou à la demande (`evo_core_refresh`) : connexion, boîte de réception
  toujours suivie (nouveau courrier), rafraîchissement des dossiers ouverts,
  puis `folders-updated` (l'interface relit les compteurs).
- **Envoi** (`evo_core_send_message`) : serveurs d'envoi SMTP ajoutés à la
  session ; identité (nom, adresse, Reply-To) et serveur d'envoi lus dans la
  source d'identité EDS du compte ; message MIME texte (UTF-8,
  quoted-printable), `In-Reply-To`/`References` pour les réponses, Cci hors
  en-têtes (destinataires du transport seulement) ; copie dans le dossier
  Envoyés de l'identité (URI `folder://…`), sauf si le serveur l'a déjà
  enregistrée. Événements `sending` / `sent`.
- `evo_core_get_message` renvoie aussi le texte brut (à citer ; HTML
  converti si besoin), `reply_to`, `message_id`, `references`.

EvolutionWin :

- Compteurs de non-lus mis à jour en direct (`FolderItem` observable via
  `INotifyPropertyChanged`), liste rechargée quand des messages arrivent
  (sélection et lecteur conservés).
- Bouton Envoyer/Recevoir (F5) ; Nouveau message (Ctrl+N).
- **Fenêtre de rédaction native** (`ComposeWindow`) : compte d'envoi, À, Cc,
  Cci, Objet, texte avec correcteur orthographique Windows ; Envoyer
  (Ctrl+Entrée) ; erreurs affichées dans la fenêtre.
- Répondre (Ctrl+R), Répondre à tous (Ctrl+Maj+R, sans sa propre adresse),
  Transférer (Ctrl+F, préfixe « TR: ») : citation `> `, fil de discussion
  conservé.
- Noms de méthodes `GetMessage`/`SendMessage` évités : macros Win32.

**Premier essai de l'utilisateur : « Mot de passe non trouvé ».** EDS
enregistre les identifiants par serveur ; celui du serveur d'envoi SMTP
n'existait pas (Evolution ne le demande qu'au premier envoi, par un
dialogue). Correctif : si le serveur d'envoi n'a pas de mot de passe
enregistré, evo-core reprend celui du compte de réception dont l'identité
envoie par ce serveur, à condition que le nom d'utilisateur soit le même
(comportement « mêmes paramètres que le serveur de réception » des autres
clients).

### Troisième jalon — actions sur les messages et recherche (2026-10-31)

evo-core :

- **Indicateurs** (`evo_core_set_message_flags`) : bascule `seen`
  (`CAMEL_MESSAGE_SEEN`) ou `flagged` (`CAMEL_MESSAGE_FLAGGED`) sur un message
  et synchronise avec le serveur (`camel_folder_synchronize_message_sync`).
- **Suppression** (`evo_core_delete_message`) : recherche le dossier corbeille
  réel du compte (`CAMEL_FOLDER_TYPE_TRASH`) ; si le message ne s'y trouve pas
  déjà, l'y déplace (`camel_folder_transfer_messages_to_sync`) et purge le
  dossier source, sinon le marque `CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN`
  et synchronise. Émet `folders-updated`.
- **Déplacement** (`evo_core_move_message`) : transfère un message vers un
  autre dossier (`camel_folder_transfer_messages_to_sync`), synchronise source
  et destination et émet `folders-updated`.
- **Recherche** (`evo_core_search_messages`) : recherche insensible à la casse
  et aux accents (`g_str_match_string` + `g_utf8_casefold`) sur Objet, De, À et
  Cc dans les résumés locaux, combinée à la recherche plein texte Camel sur le
  corps des messages (`camel_folder_search_sync` avec `body-contains`).

EvolutionWin :

- Barre de commandes du volet de lecture complétée : **Lu / Non lu** (`Ctrl+U`),
  **Suivi** (drapeau), **Déplacer** (menu déroulant hiérarchique des dossiers
  du compte), **Supprimer** (`Suppr`).
- Liste des messages : icônes d'indicateur de suivi (drapeau) et de pièce
  jointe (trombone) sur chaque ligne ; **menu contextuel** (clic droit) avec
  Lu/Non lu, Drapeau, Déplacer vers et Supprimer ; sélection automatique du
  message suivant après suppression ou déplacement.
- Zone de **recherche** (`SearchBox`) : filtrage instantané en mémoire au fil
  de la frappe (`TextChanged`) et recherche complète dans le dossier (en-têtes
  + corps des messages via `evo-core`) à la validation (`Entrée` ou loupe).

### Quatrième jalon — notifications Windows et pièces jointes (2026-10-31)

evo-core :

- **Notifications de nouveau courrier** (`new-mail`) : lors de la synchronisation
  des boîtes de réception (après la première synchronisation initiale au
  démarrage), `evo-core` détecte les nouveaux UID ajoutés et non lus et émet un
  événement `new-mail` contenant `account`, `folder`, `count`, `from` et
  `subject`.
- **Liste et téléchargement des pièces jointes** : `evo_core_get_message`
  retourne désormais un tableau `attachments` (`index`, `filename`,
  `mime_type`, `size`) pour toutes les parties MIME jointes (ou non-inline hors
  corps principal). Nouvelle fonction `evo_core_save_attachment` qui décode et
  écrit la pièce jointe choisie sur le disque via `g_file_set_contents`.
- **Envoi de pièces jointes** : `evo_core_send_message` accepte un tableau
  optionnel `attachments` de chemins locaux et construit un message
  `multipart/mixed` combinant le corps et chaque fichier encodé en Base64
  (`CamelMimePart` avec nom de fichier et type MIME détecté).

EvolutionWin :

- Réception de `new-mail` : notification système Windows (toast via
  `AppNotificationBuilder` / `AppNotificationManager`) et rappel dans la barre
  d'état.
- Volet de lecture : barre horizontale `AttachmentBar` listant chaque pièce
  jointe avec sa taille ; clic ouvrant un `FileSavePicker` natif (initialisé
  avec le `HWND` de la fenêtre) pour enregistrer le fichier.
- Fenêtre de rédaction (`ComposeWindow`) : bouton **Joindre** (`FileOpenPicker`
  multi-fichiers), barre de jetons permettant de visualiser ou retirer les
  pièces jointes avant l'envoi.

### Cinquième jalon — gestion des comptes et authentification (2026-10-31)

evo-core :

- **Demande de mot de passe** (`auth-required`) : lorsque le Gestionnaire
  d'identifiants Windows ne possède pas le mot de passe d'une source ou que le
  serveur le refuse, `evo-core` émet un événement `auth-required` (`source_uid`,
  `name`, `user`, `rejected`) dédoublonné par session.
- **Enregistrement du mot de passe** (`evo_core_save_password`) : stocke de
  façon permanente le secret dans le Gestionnaire d'identifiants Windows via
  `e_source_credentials_provider_store_sync` et réarme la source pour la
  prochaine tentative.
- **Ajout et suppression de compte** (`evo_core_add_account`,
  `evo_core_remove_account`) : création atomique des 3 sources EDS d'un compte
  de messagerie (`ESourceMailAccount` + `CamelImapxSettings`,
  `ESourceMailIdentity` + `ESourceMailSubmission`, `ESourceMailTransport` +
  `CamelSmtpSettings`) via `e_source_registry_create_sources_sync`, stockage du
  mot de passe et ajout immédiat des services à la `CamelSession`. Suppression
  complète des 3 sources et déconnexion des services correspondants.

EvolutionWin :

- Boîte de dialogue modale `ContentDialog` automatique sur l'événement
  `auth-required` permettant de saisir et d'enregistrer le mot de passe puis de
  relancer la synchronisation.
- Vue **Paramètres / Comptes de messagerie** (`SettingsViewPane`) : liste des
  comptes configurés (adresse, protocole, suppression des comptes non locaux)
  et assistant **Ajouter un compte IMAP / SMTP** (nom du compte, nom complet,
  adresse électronique, serveurs et ports IMAP/SMTP, mot de passe).

### Sixième jalon — calendrier et contacts EDS (2026-10-31)

evo-core (lié à `libecal-2.0` et `libebook-1.2`) :

- **Calendrier** (`evo_core_list_events`, `evo_core_create_event`,
  `evo_core_delete_event`) : connexion aux sources `E_SOURCE_EXTENSION_CALENDAR`
  via `e_cal_client_connect_sync` (`E_CAL_CLIENT_SOURCE_TYPE_EVENTS`), requête
  des composants `ICalComponent` (`VEVENT`) et conversion des dates en
  timestamps Unix, création et suppression d'événements.
- **Contacts** (`evo_core_list_contacts`, `evo_core_create_contact`,
  `evo_core_delete_contact`) : connexion aux sources
  `E_SOURCE_EXTENSION_ADDRESS_BOOK` via `e_book_client_connect_sync`, recherche
  filtrée (`EBookQuery`) sur le nom et l'adresse électronique, lecture des
  champs `EContact` (`full_name`, `email`, `phone`, `org`, `note`), ajout et
  suppression de contacts.

EvolutionWin :

- Vue **Calendrier** (`CalendarViewPane`) : `CalendarView` mensuel pour filtrer
  par jour (ou afficher les 30 prochains jours), liste des événements avec
  horaires, lieu, description, suppression et boîte de dialogue **Nouvel
  événement** (`CalendarDatePicker`, `TimePicker`).
- Vue **Contacts** (`ContactsViewPane`) : recherche instantanée, liste des
  contacts, fiche détaillée avec actions **Écrire un message** (ouvre
  `ComposeWindow` pré-remplie avec le destinataire) et **Supprimer le contact**,
  ainsi qu'une boîte de dialogue **Nouveau contact**.

### Septième jalon — éditeur HTML (WebView2), impression native et réorganisation du dépôt (2026-10-31)

Réorganisation du dépôt (`Evolution-WindowsPort`) :

- `WINDOWS-PORT.md`, `README.md` et `.gitignore` placés à la racine du dépôt.
- Scripts et outils de portage MSVC regroupés dans `./tools` (`build-*.bat`, `env.bat`, `check-prereqs.ps1`, `cmake/`, `compat/`, `patches/`, `pkgconfig/`, `webkit-shim/`, `tools/nuget.exe`).
- Client natif WinUI 3 déplacé à la racine dans `./winui` (`winui/core` et `winui/app`).
- Répertoire `./build` contenant tous les exécutables finaux prêts à l'emploi (`EvolutionWin.exe`, `evo-core.dll`, `evo-core-test.exe`, `evolution.exe`, `libexec/*.exe`) et `./build/intermediate` (ignoré par Git) pour les fichiers d'objets CMake/MSBuild.
- Pile GTK (`./gtk-build`) intégrée à la racine du projet et détectée automatiquement par `tools\env.bat`.

evo-core :

- **Envoi de messages HTML (`multipart/alternative`)** : `evo_core_send_message` accepte désormais un champ optionnel `"html"` en complément de `"text"`. Lorsque `"html"` est fourni, le moteur construit un conteneur `multipart/alternative` combinant la partie `text/plain; charset=utf-8` (générée automatiquement via `html_to_text` si `"text"` est vide) et la partie `text/html; charset=utf-8` (toutes deux encodées en `quoted-printable`). Si des pièces jointes (`"attachments"`) sont présentes, le `multipart/alternative` constitue la première partie du conteneur `multipart/mixed`.

EvolutionWin :

- **Éditeur HTML riche sur WebView2** (`ComposeWindow`) :
  - Zone d'édition `contenteditable="true"` propulsée par WebView2 (`HtmlEditor`) avec correcteur orthographique activé, police Segoe UI et transfert du raccourci `Ctrl+Entrée` vers l'envoi du message via `WebMessageReceived`.
  - Barre d'outils de mise en forme (`FormatToolbar`) : **Gras** (`Ctrl+B`), **Italique** (`Ctrl+I`), **Souligné** (`Ctrl+U`), **Barré**, **Liste à puces**, **Liste numérotée**, **Bloc de citation** (`blockquote`), **Insérer un lien hypertexte** (boîte de dialogue `ContentDialog` demandant l'URL) et **Effacer la mise en forme**.
  - Bouton à bascule **Format HTML** dans la barre de commandes permettant de passer à tout moment du mode HTML riche au mode Texte brut (`TextBox`) avec conversion automatique du contenu saisi.
  - Citations HTML structurées (`<blockquote>` stylé pour les réponses, en-tête formaté pour les transferts) préparées par `MainWindow::Reply`.
- **Impression native des messages** (`MainWindow`) : bouton **Imprimer** (`Ctrl+P`) dans la barre de commandes du volet de lecture ouvrant l'interface d'impression native de WebView2 (`CoreWebView2::ShowPrintUI`).

### Huitième jalon — certificats TLS, OAuth2, S/MIME, OpenPGP, sélection multiple (2026-10-31)

evo-core :

- **Certificats TLS** : `EvoCoreSession::trust_prompt` émet `tls-trust-required`
  (hôte, empreinte SHA-256, émetteur, validité, anomalies décrites en français)
  et attend la réponse (`evo_core_respond_tls_trust` : refuser, accepter pour la
  session, toujours faire confiance). Les certificats approuvés de façon
  permanente sont enregistrés dans `…\evolution-windows\mail\trusted-certs.ini`.
- **OAuth2 (XOAUTH2) pour IMAP/SMTP** : `get_oauth2_access_token_sync` lit le
  jeton dans le Gestionnaire d'identifiants (JSON `access_token`,
  `refresh_token`, `expires_at`, `client_id`, `token_endpoint`) et le
  renouvelle avec le `refresh_token` (libsoup) une minute avant expiration.
  `evo_core_add_account` accepte `auth_mechanism` (`XOAUTH2`).
- **S/MIME et OpenPGP à la lecture** : déchiffrement (`multipart/encrypted`,
  `application/pkcs7-mime`) puis vérification de signature (`multipart/signed`),
  résultat dans l'objet `security` du message.
- **S/MIME et OpenPGP à l'envoi** : options `pgp_sign`, `pgp_encrypt`,
  `smime_sign`, `smime_encrypt` (et `smime_key_id`) de `evo_core_send_message`.
- **Export `.eml`** (`evo_core_export_messages_eml`) : messages écrits en
  RFC 822 dans un dossier temporaire, pour un transfert en pièces jointes.

EDS (`camel-gpg-context.c`, phase 4c) :

- `gpg.exe` recherché dans le PATH puis dans les dossiers d'installation de
  GnuPG / Gpg4win ; lancé par `g_spawn_async_with_pipes()` (utilitaire
  `gspawn-win64-helper.exe`), lignes de statut sur `--status-fd=2`, mêlées aux
  diagnostics de gpg et triées à l'analyse (CRLF retiré).
- Sorties lues sans blocage (`PeekNamedPipe`). L'entrée standard est écrite par
  un **thread dédié** : les tubes anonymes Windows n'ont pas d'écriture non
  bloquante, et écrire depuis la boucle provoquerait un interblocage dès que
  gpg remplit sa sortie (message de quelques Ko).
- Phrase secrète : gpg tourne en `--batch`, c'est gpg-agent qui l'obtient par
  sa fenêtre pinentry (celle de Gpg4win).
- **Test** (2026-10-31, `gpg` 2.4.9 de Git for Windows, clé de test) :
  signature, vérification (« bonne signature »), chiffrement et déchiffrement
  d'un message de 1 Mo en 0,1 à 0,3 s ; contenu identique (Camel met le texte
  en forme canonique CRLF avant signature/chiffrement, comme sous Linux).

EvolutionWin :

- Dialogue **certificat TLS** (`PromptTlsCertificateAsync`) : détails du
  certificat, Refuser / Accepter pour cette session / Toujours faire confiance.
- Assistant d'ajout de compte : authentification **OAuth2** Microsoft 365 ou
  Google (PKCE S256, page de connexion dans un WebView2, redirection
  `http://localhost/oauth2callback`). Identifiant d'application et tenant
  préremplis par `EVO_OAUTH2_CLIENT_ID` / `EVO_OAUTH2_TENANT_ID`.
- **Bandeau de sécurité** du volet de lecture (signature valide ou non,
  message chiffré) ; boutons **Signer / Chiffrer (PGP)** et **Signer / Chiffrer
  (S/MIME)** dans la fenêtre de rédaction.
- **Sélection multiple** dans la liste : lu/non lu, drapeau, déplacement,
  suppression, réponse et transfert de plusieurs messages (transfert en
  pièces jointes `.eml`).

Problèmes rencontrés :

1. **Scripts `.bat` illisibles par `cmd.exe`** (`'ot' n'est pas reconnu…`) :
   fichiers en fins de ligne LF. `.gitattributes` impose désormais CRLF aux
   `.bat`/`.cmd` (et LF aux scripts bash), quel que soit `core.autocrlf`.
2. **`co_await` dans un bloc `catch`** (C2304, échange du jeton OAuth2) :
   message d'erreur mémorisé, affiché après le bloc.
3. **Aucun compte au démarrage** (« No provider available for protocol imapx ») :
   une ancienne copie de `camel-1.2.dll` dans `build\` était chargée en
   priorité ; Camel calcule ses chemins depuis l'emplacement de sa DLL et
   cherchait ses fournisseurs dans `build\lib\…`. **Ne pas copier de DLL d'EDS
   dans `build\`** : elles doivent être chargées depuis le préfixe (PATH, comme
   le fait `run-winui.bat`) tant que l'installeur (phase 5) n'a pas construit
   une arborescence complète. Un ancien `evolution-source-registry` lancé depuis
   `C:\gtk-build` (avant la réorganisation) occupait aussi le bus D-Bus.

4. **Boîte de réception vide au premier lancement** (VM Windows 11 neuve,
   MSI 0.9.0 ; autres dossiers corrects) : la relève de démarrage ouvrait la
   boîte de réception par `camel_store_get_inbox_folder_sync`, dont
   l'implémentation par défaut (IMAP) crée un **second `CamelFolder` hors du
   cache de dossiers du magasin**. evo-core suivait cette instance ; les
   messages téléchargés par le serveur IMAP allaient dans l'autre (celle du
   cache) : la liste restait vide, sans erreur. Avec un cache existant, les
   deux instances relisent le même résumé sur disque, d'où un problème
   invisible sur le poste de développement. Correctif : nom de la boîte de
   réception lu dans l'arborescence (`CAMEL_FOLDER_TYPE_INBOX`), dossier
   ouvert comme les autres (`camel_store_get_folder_sync`). De plus,
   `core_ensure_connected` sérialise les connexions et vérifie l'état
   `CONNECTED` : `camel_offline_store_set_online_sync` répond « succès »
   immédiatement si un autre fil est en train de connecter, et une relève
   IMAP lancée alors est ignorée sans erreur. Reproduit avec un cache vide
   (variables `XDG_DATA_HOME` / `XDG_CACHE_HOME`) et une seconde liste
   déclenchée par `folder-changed`, comme le fait l'interface : 3 échecs
   sur 10 avant, 0 sur 15 après (MSI 0.9.1).

**Validé** : `evo-core-test` (lecture seule) avec le compte IMAP réel :
comptes, dossiers, 56 messages, lecture d'un message. Reste à valider dans
l'interface : dialogue TLS, connexion OAuth2 (inscription d'application
Entra ID ou Google requise), S/MIME (certificat personnel dans NSS) et
OpenPGP avec Gpg4win.

### Prochaines étapes du client natif

1. ~~Rafraîchissement automatique~~ ; ~~rédaction et envoi~~ (texte brut, HTML `multipart/alternative` et pièces jointes).
2. ~~Actions : supprimer, déplacer, marquer lu/non lu, drapeau ; recherche.~~
3. ~~Notifications Windows (toasts) pour le nouveau courrier et pièces jointes.~~
4. ~~Gestion des comptes (assistant d'ajout IMAP/SMTP, mot de passe) et paramètres.~~
   ~~Configurables directement dans l'application.~~
5. ~~Calendrier et contacts (EDS) : `CalendarView`, listes et fiches de contacts.~~
6. ~~Éditeur HTML de composition (WebView2) et impression native des messages (`Ctrl+P`).~~
7. ~~Dialogue de certificats TLS, OAuth2 IMAP/SMTP, S/MIME, OpenPGP~~ (validation
   dans l'interface à faire).
8. ~~Installeur MSI (phase 5).~~
9. Microsoft 365 par Microsoft Graph (evolution-ews, phase 4b).

## Suivi des phases

- [x] Phase 0 — pile GTK3 MSVC (terminée le 2026-10-31, ~45 min ; gvsbuild a
  aussi construit gtk4, requis par adwaita-icon-theme)
- [x] NSPR / NSS compilés avec MSVC
- [x] libical / libical-glib compilés avec MSVC
- [x] Phase 1 — EDS compilé et registre des sources démarré (fabriques
  carnet d'adresses / agenda : validées avec EvolutionWin)
- [x] Phase 2 — Evolution compilé (shim WebKit vide) et lancé : fenêtre
  principale fonctionnelle (2026-10-31)
- [x] Phase 2b — démarrage automatique des services EDS ; compte IMAP réel
  synchronisé (envoi SMTP validé)
- [x] Phase 3 — EWebView sur WebView2 (lecture des mails) : fait pour la
  version GTK transitoire (2026-10-31)
- [x] Phase 4 (WinUI 3) — éditeur HTML WebView2 et impression native réalisés dans `EvolutionWin` (S/MIME à venir)
- [~] Phase 4b — evolution-ews compilé (MSVC), comptes Microsoft 365 (Graph,
  OAuth2 WebView2) dans EvolutionWin ; test avec un vrai tenant à faire
- [x] Phase 4c — OpenPGP : `camel-gpg-context.c` porté sur
  `g_spawn_async_with_pipes()` (testé le 2026-10-31) ; reste à intégrer
  Gpg4win comme prérequis optionnel de l'installeur MSI
- [x] Phase 5 — installeur MSI (WiX v5) autonome, validé par extraction
  (2026-10-31) ; icône, associations et `mailto:` à venir
- [—] Phase 6a — abandonnée (la version GTK reste transitoire)
- [x] Phase 6b — **choisie** : interface réécrite en WinUI 3 / C++/WinRT (jalons 1 à 7 réalisés : courrier, lecture WebView2, rédaction HTML/texte et envoi, impression, actions, recherche, notifications toasts, pièces jointes, gestion des comptes et mots de passe, calendrier et contacts EDS)
