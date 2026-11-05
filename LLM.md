# Guide de Référence pour Agent IA — EvolutionWin (Evolution Windows Port)

Ce document constitue le manuel de référence complet destiné à tout agent IA ou développeur reprenant le développement d'**EvolutionWin**. Il résume l'architecture, l'environnement, les règles de contribution, les commandes de compilation et de test, ainsi que l'état d'avancement du projet.

---

## 1. Vue d'Ensemble & Identité du Projet

**EvolutionWin** est un portage natif et moderne sous **Windows 11** de la suite collaborative Evolution (Client de messagerie, Calendrier, Contacts et Gestionnaire de tâches de GNOME).

- **Moteur sous-jacent** : `evolution-data-server` (EDS), Camel (pile protocolaire IMAP, SMTP, POP3, EWS), GLib/GObject, libxml2, libsoup-3.0, libical.
- **Pont natif (C)** : `winui/core/evo-core.dll` encapsule les types et boucles d'événements GLib/EDS pour exposer une API C propre basée sur des échanges JSON et des rappels asynchrones.
- **Interface graphique** : **WinUI 3** (Windows App SDK 1.7) en C++/WinRT, respectant fidèlement le style visuel Windows 11 (Fluent Design, Mica, mode sombre/clair, contrôles modernes).
- **Utilisateur & Propriétaire** : **Pierre** (le tutoyer, communiquer en français).

---

## 2. Environnement de Développement & Prérequis

- **Système d'exploitation** : Windows 11 x64 (22H2 ou ultérieur).
  > **Note impérative** : Il s'agit d'une machine Windows 11. Ne jamais exécuter de commandes de type Linux/Bash (utiliser PowerShell `pwsh` ou des scripts `.bat`).
- **Compilateur & Outils** :
  - Visual Studio 2026 Community (MSVC v180, C++20 / C++latest).
  - CMake 3.25+ & Ninja.
  - Windows App SDK 1.7 (packages NuGet natifs intégrés dans `winui/packages/`).
  - WiX Toolset v3.14 (pour la génération des installeurs MSI).
  - Python 3.10+ (pour les scripts de maintenance et tests).

---

## 3. Arborescence du Dépôt

```
c:\Users\chris\VSCode\Evolution-WindowsPort\
├── VERSION                       # Numéro de version SemVer courante (ex: 0.9.8)
├── ROADMAP.md                    # Feuille de route par jalons de versions (v0.9.8, v1.0.0...)
├── CHANGELOG.md                  # Historique des modifications (Keep a Changelog)
├── TODO.md                       # Référentiel exhaustif des spécifications et tâches
├── QUICKWINS.md                  # Tâches prioritaires à fort impact (évolutif)
├── TESTS.md                      # Guide et spécification de la suite de tests
├── locks/                        # Verrous locaux JSON anti-collision multi-agents (ignoré Git)
│
├── winui/
│   ├── core/                     # Moteur C (evo-core.dll), découpé par domaine fonctionnel
│   │   ├── evo-core.h            # API publique (déclarations et exports ABI)
│   │   ├── evo-core-private.h    # En-tête privé : includes, struct EvoCore, types et prototypes partagés
│   │   ├── evo-core.c            # Cycle de vie : journal, modules EDS, fil GLib, new/free, hors connexion
│   │   ├── evo-core-session.c    # CamelSession : authentification, certificats TLS
│   │   ├── evo-core-events.c     # Constructeurs JSON et émission des événements
│   │   ├── evo-core-requests.c   # File des requêtes asynchrones (core_submit), magasins, connexion
│   │   ├── evo-core-accounts.c   # Liste, compte par défaut, suppression
│   │   ├── evo-core-account-setup.c  # Ajout, test, lecture, modification IMAP/POP3/SMTP, mots de passe
│   │   ├── evo-core-account-cloud.c  # Comptes Microsoft 365 / Exchange (EWS) et Google
│   │   ├── evo-core-detect.c     # Détection automatique (ISPDB, DNS, Autodiscover)
│   │   ├── evo-core-oauth2.c     # Connexion OAuth2 et demandes d'identifiants
│   │   ├── evo-core-folders.c    # Arborescence, rôles, suivi des dossiers ouverts
│   │   ├── evo-core-folder-ops.c # Création, renommage, déplacement, copie, suppression, quota
│   │   ├── evo-core-messages.c   # Liste des messages d'un dossier
│   │   ├── evo-core-search.c     # Recherche (dossier, sous-dossiers, compte, tous) et vues unifiées multi-comptes
│   │   ├── evo-core-message-read.c    # Lecture, pièces jointes, export .eml
│   │   ├── evo-core-message-actions.c # Indicateurs, suivi, déplacement, copie, suppression
│   │   ├── evo-core-refresh.c    # Relève, synchronisation périodique, reconnexion
│   │   ├── evo-core-send.c       # Envoi MIME et Boîte d'envoi
│   │   ├── evo-core-drafts.c     # Brouillons
│   │   ├── evo-core-signatures.c # Signatures
│   │   ├── evo-core-calendar.c   # Événements d'agenda
│   │   ├── evo-core-tasks.c      # Tâches (VTODO)
│   │   ├── evo-core-contacts.c   # Contacts et autocomplétion des destinataires
│   │   ├── evo-core-sources.c    # Sources EDS et création d'agendas
│   │   ├── evo-core-export.c     # Export vCard / iCalendar
│   │   └── CMakeLists.txt        # Configuration CMake du moteur
│   └── app/                      # Interface WinUI 3 (C++/WinRT)
│       ├── App.xaml / App.xaml.cpp
│       ├── MainWindow.xaml / .h  # Fenêtre principale et vue d'ensemble
│       ├── MainWindow.*.cpp      # Découpage modulaire du code-behind :
│       │   ├── MainWindow.Accounts.cpp       # Comptes, synchronisation, quota
│       │   ├── MainWindow.Attachments.cpp    # Pièces jointes, aperçu, export EML
│       │   ├── MainWindow.Calendar.cpp       # Grilles Jour/Semaine/Mois, filtres
│       │   ├── MainWindow.Compose.cpp        # Commandes d'ouverture de rédaction
│       │   ├── MainWindow.Contacts.cpp       # Carnets d'adresses et cartes
│       │   ├── MainWindow.Events.cpp         # Événements et rappels de calendrier
│       │   ├── MainWindow.Folders.cpp        # Arborescence des dossiers et actions
│       │   ├── MainWindow.Layout.cpp         # Volets, séparateurs, disposition
│       │   ├── MainWindow.MessageActions.cpp # Répondre, transférer, archiver, indésirables
│       │   ├── MainWindow.MessageFilter.cpp  # Filtres de lecture, recherche rapide
│       │   ├── MainWindow.MessageList.cpp    # Grille des messages, conversations
│       │   ├── MainWindow.Reader.cpp         # Volet de lecture WebView2, zoom
│       │   ├── MainWindow.Ribbon.cpp         # Ruban Fluent et onglets contextuels
│       │   ├── MainWindow.Settings.cpp       # Panneau des paramètres applicatifs
│       │   ├── MainWindow.Signatures.cpp     # Gestionnaire des signatures HTML/texte
│       │   ├── MainWindow.Sources.cpp        # Sources EDS de calendriers/contacts
│       │   ├── MainWindow.StatusBar.cpp      # Barre d'état, progression, annulation
│       │   └── MainWindow.Tasks.cpp          # Listes de tâches et mémos
│       ├── MainWindow.Helpers.h  # Algorithmes autonomes, parsing MIME, localisation
│       ├── CoreBridge.h          # Passerelle C++/WinRT encapsulant evo-core.dll
│       ├── Settings.h            # Persistance JSON des réglages et préférences
│       ├── UnifiedViews.h        # Vues unifiées multi-comptes : fonctions pures (uid composite, comptes inclus)
│       ├── ComposeWindow.xaml / .cpp / .h # Fenêtre de rédaction de courriel
│       ├── MessageWindow.xaml / .cpp / .h # Fenêtre autonome de visualisation de message
│       └── EvolutionWin.vcxproj  # Projet MSBuild pour l'application WinUI
│
├── tests/
│   ├── unit/
│   │   ├── test_helpers.cpp             # Tests unitaires WinUI & Helpers (54 tests)
│   │   ├── test_evo_core_contracts.c    # Tests contrats JSON & robustesse C (12 tests)
│   │   └── test_settings.cpp            # Tests persistance des préférences (5 tests)
│   └── abi/
│       └── test_abi_exports.ps1         # Validation dynamique de l'ABI d'evo-core.dll (67 exports)
│
├── tools/
│   ├── build-winui-core.bat      # Compilation de evo-core.dll (CMake/Ninja)
│   ├── build-winui-app.bat       # Compilation de EvolutionWin.exe (MSBuild)
│   ├── run-tests.bat             # Exécution complète de la suite de tests (100% SUCCÈS exigé)
│   ├── build-msi.ps1             # Génération du package d'installation MSI (WiX)
│   ├── publish-release.ps1       # Publication des artefacts MSI sur GitHub Releases
│   └── run-winui.bat             # Lancement rapide de l'application compilée
│
└── gtk-build/ / evolution-data-server/ # Binaires de support et sources sous-jacentes
```

---

## 4. Commandes Essentielles (Cycle de Développement)

Chaque commande doit être exécutée depuis la racine du dépôt :

### 4.1. Compiler le moteur C (`evo-core.dll`)
```powershell
.\tools\build-winui-core.bat
```
- Génère `build\evo-core.dll` et `build\evo-core.lib` via CMake et Ninja en quelques secondes.

### 4.2. Compiler l'application WinUI (`EvolutionWin.exe`)
```powershell
.\tools\build-winui-app.bat
```
- Compile l'application WinUI 3 en Release x64 via `MSBuild`.

### 4.3. Exécuter l'intégralité des tests unitaires
```powershell
.\tools\run-tests.bat
```
- Compile et lance successivement :
  1. `test_helpers.exe` (54 tests algorithmiques).
  2. `test_evo_core_contracts.exe` (12 tests C de robustesse et contrats JSON).
  3. `test_settings.exe` (5 tests de sérialisation et résilience).
  4. `test_abi_exports.ps1` (contrôle strict des fonctions exportées par la DLL).
- **Règle absolue** : 100% des tests doivent être au vert avant tout commit.

### 4.4. Lancer l'application
```powershell
.\tools\run-winui.bat
```

### 4.5. Construire le MSI d'installation
```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build-msi.ps1
```
- Génère `build\Evolution-Setup-<version>.msi`.

---

## 5. Règles Strictes de Développement & Contribution

Les règles suivantes ont été définies par Pierre et doivent être respectées à la lettre :

1. **Langue française obligatoire** :
   - Tous les échanges, documentations, interfaces graphiques, messages de commit et commentaires de code doivent être en **français**.
   - Le code doit être systématiquement commenté (doxygen `@brief`, `@param`, `@return`).
   - Penser à déspécialiser les apostrophes dans les sorties console si nécessaire.

2. **Stratégie de Git & Commits** :
   - Travailler sur la branche active de la version en cours (actuellement `0.9.8`).
   - Réaliser **un commit atomique après chaque tâche traitée**.
   - Format de commit conventionnel : `feat(<scope>): <description en français>` ou `fix(<scope>): <description>`.
   - Toujours pousser (`git push origin <branche>`) après avoir validé un ensemble de tâches.

3. **Mise à jour documentaire continue** :
   - Dès qu'une tâche est achevée, cocher sa case dans `TODO.md` (`- [x]`).
   - Ajouter un paragraphe explicatif sous la section `## [<version>] — en cours` dans `CHANGELOG.md`.

---

## 6. Architecture & Modèles de Conception

### 6.1. Le Pont `evo-core` (C) <-> `CoreBridge` (C++/WinRT)
- Toutes les opérations d'E/S (réseau, disque, protocoles IMAP/SMTP) s'exécutent sur des threads d'arrière-plan via `core_submit()` (`evo-core-requests.c`).
- Le moteur est découpé en un fichier par domaine (`evo-core-<domaine>.c`) : chaque fonction publique `evo_core_xxx` est définie à côté de son exécutant `core_xxx_run`. Les fonctions internes partagées entre fichiers (préfixe `core_`) sont déclarées dans `evo-core-private.h` ; toute nouvelle fonction partagée y est ajoutée, et tout nouveau fichier `.c` est ajouté à `winui/core/CMakeLists.txt`. `evo-core.h` reste la seule interface publique.
- La communication repose sur du **JSON** :
  - Entrée : chaîne JSON sérialisée ou paramètres typés C.
  - Sortie : fonction de rappel `EvoCoreResultFunc` de type `void (const char *json_result, const char *error_message, void *user_data)`.
- `CoreBridge` (`winui/app/CoreBridge.h`) reçoit le résultat et ré-aiguille automatiquement l'exécution sur le **thread d'interface WinUI** via `DispatcherQueue::TryEnqueue()`.

### 6.2. Préférences et Paramètres (`Settings.h`)
- Les réglages sont stockés au format JSON dans `%LOCALAPPDATA%\EvolutionWin\settings.json`.
- Accès singleton via `EvolutionWin::Settings::Instance()`.
- Méthodes typées : `GetBoolean()`, `SetBoolean()`, `GetString()`, `SetString()`, `GetInt()`, `SetInt()`, `GetArray()`, etc.
- Toujours fournir une valeur de repli par défaut lors de la lecture.

### 6.3. Localisation et Unicode sous Windows MSVC
- **Attention au pliage de casse Unicode (accents)** : `std::towlower` en C++ sous Windows n'applique pas la casse française par défaut. Toujours utiliser `::CharLowerBuffW(str.data(), len)` (provenant de `user32.lib`) pour les recherches insensibles à la casse et aux accents.
- **Formatage des tailles de fichiers** : Utiliser l'API système `::StrFormatByteSizeW` (via `shlwapi.lib`) pour respecter les unités localisées par Windows (ex: "1,42 Mo" en français, "1.42 MB" en anglais).

---

## 7. Pilotage Dynamique des Tâches & Source de Vérité

Ce fichier `LLM.md` ne contient volontairement **aucun listing statique de tâches faites ou à faire** afin d'éviter toute désynchronisation. L'état du projet et les fonctionnalités sont pilotés dynamiquement par quatre fichiers clés que l'agent IA doit obligatoirement consulter à chaque prise en main :

> [!IMPORTANT]
> **Nature vivante et évolutive des fichiers de tâches** :
> Les fichiers [`TODO.md`](TODO.md) et [`QUICKWINS.md`](QUICKWINS.md) **ne sont jamais statiques ni figés**. Pierre peut les modifier ou y injecter régulièrement de nouvelles tâches, exigences ou priorités au fil de l'eau. L'agent IA doit toujours relire l'état réel et à jour de ces fichiers plutôt que de se baser sur un souvenir ou une supposition.

### 7.1. Fichiers sources à lire obligatoirement :
1. **[`VERSION`](VERSION)** : Indique la version SemVer courante du projet (ainsi que le nom de la branche de développement active, ex. `0.9.8`).
2. **[`ROADMAP.md`](ROADMAP.md)** : **Feuille de route stratégique par jalons de version** (v0.9.8, v1.0.0, v1.1.0, etc.). Elle indique à l'agent IA **quelles tâches de `TODO.md` traiter en priorité** pour la version cible en cours.
3. **[`CHANGELOG.md`](CHANGELOG.md)** : Registre historique officiel listant l'ensemble des fonctionnalités déjà réalisées, testées et validées, regroupées par version.
4. **[`QUICKWINS.md`](QUICKWINS.md)** : Liste des tâches prioritaires à fort impact utilisateur et faible coût d'implémentation. Ce fichier évolue régulièrement avec de nouveaux ajouts de Pierre ; l'agent doit toujours y traiter en priorité absolue les tâches ouvertes (`- [ ]`).
5. **[`TODO.md`](TODO.md)** : Référentiel global et exhaustif du projet, détaillant les spécifications complètes, les contraintes métier et l'état d'avancement de chaque module. Pierre y ajoute régulièrement de nouvelles fonctionnalités à développer.

### 7.2. Règles de traitement des tâches pour l'agent IA :
- **Identification des besoins** : L'agent consulte d'abord [`ROADMAP.md`](ROADMAP.md) pour identifier les objectifs du jalon de version actif, puis recherche les tâches **non cochées** (`- [ ]`) correspondantes dans [`QUICKWINS.md`](QUICKWINS.md) et [`TODO.md`](TODO.md).
- **Mise à jour obligatoire lors de la complétion** :
  - Dès qu'une tâche est développée et validée par la suite de tests (`tools\run-tests.bat`), elle doit **obligatoirement être cochée** (`- [x]`) dans les fichiers sources concernés ([`ROADMAP.md`](ROADMAP.md), [`QUICKWINS.md`](QUICKWINS.md) et/ou [`TODO.md`](TODO.md)).
  - L'entrée correspondante doit **obligatoirement être consignée dans [`CHANGELOG.md`](CHANGELOG.md)** sous la section `## [<version>] — en cours` avec une description claire de l'apport fonctionnel et technique.
  - La tâche fait l'objet d'un **commit atomique dédié** avant de passer à la tâche suivante.

---

## 8. Travail Multi-Agents en Parallèle & Stratégie Anti-Collision (Verrous Locaux)

Plusieurs agents IA indépendants peuvent travailler **en parallèle sur la machine locale dans ce même espace de travail**. Afin d'éviter les collisions de code, les régressions et les modifications concurrentes sur les mêmes fichiers, chaque agent applique un protocole de verrouillage local léger via le dossier [`locks/`](locks/).

> [!NOTE]
> Les fichiers `locks/*.json` sont exclus du versionnement via [`.gitignore`](.gitignore). Ils vivent uniquement sur le disque local pour coordonner les agents en temps réel sans polluer l'historique Git.

### 8.1. Registre local des verrous (`locks/`)
Chaque agent actif signale sa tâche en créant directement sur disque un fichier JSON :
`locks/<agent_id>_<identifiant_tache>.json`

**Format JSON standardisé obligatoire pour tout verrou :**
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

**Description des champs JSON :**
- `agent_id` *(chaîne)* : identifiant unique de l'agent qui travaille.
- `task_ref` *(chaîne)* : référence explicite à la tâche dans `ROADMAP.md`, `TODO.md` ou `QUICKWINS.md`.
- `description` *(chaîne)* : court résumé en français de l'objectif fonctionnel.
- `files` *(tableau de chaînes)* : liste exhaustive des fichiers du dépôt que l'agent s'apprête à modifier.
- `functions_or_sections` *(chaîne)* : noms des méthodes, classes ou portions de code affectées pour alerter les autres agents sur le périmètre précis.
- `started_at` *(chaîne ISO 8601)* : horodatage UTC de début de la prise en charge.

### 8.2. Protocole obligatoire à chaque tâche (Cycle en 5 étapes) :

1. **Vérification de l'espace de travail** :
   S'assurer que le dépôt local est propre via `git status` et que la branche est à jour.
2. **Vérification de non-collision** :
   Lire tous les fichiers `.json` présents dans le répertoire `locks/` :
   - Si un autre agent a réservé les mêmes fichiers (ou des fichiers directement dépendants), **choisir une autre tâche indépendante** dans [`QUICKWINS.md`](QUICKWINS.md) ou [`TODO.md`](TODO.md).
   - Si un fichier de verrou date de plus de 4 heures sans activité, il peut être considéré comme orphelin et nettoyé.
3. **Pose immédiate du verrou local** :
   - Créer le fichier `locks/<agent_id>_<task_id>.json` avec la liste exacte des fichiers ciblés. Pas besoin de commit Git (le fichier est ignoré).
4. **Développement, Compilation & Tests** :
   - Réaliser les modifications de code sur le périmètre réservé.
   - Compiler le moteur C (`.\tools\build-winui-core.bat`) et l'application (`.\tools\build-winui-app.bat`).
   - Valider l'intégralité de la suite de tests (`.\tools\run-tests.bat`). **100% de réussite exigé**.
5. **Libération du verrou, Documentation & Commit** :
   - **Supprimer son fichier de verrou** dans `locks/`.
   - Cocher la tâche dans [`QUICKWINS.md`](QUICKWINS.md) ou [`TODO.md`](TODO.md) (`- [x]`).
   - Ajouter l'entrée descriptive dans [`CHANGELOG.md`](CHANGELOG.md).
   - Commiter la fonctionnalité (`git add ... && git commit -m "feat(...): ..."`).
   - Pousser le commit vers `origin/<branche_active>`.


