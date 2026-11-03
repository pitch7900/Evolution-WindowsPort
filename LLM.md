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
  - Visual Studio 2022 Community (MSVC v143 / v180, C++20).
  - CMake 3.25+ & Ninja.
  - Windows App SDK 1.7 (packages NuGet natifs intégrés dans `winui/packages/`).
  - WiX Toolset v3.14 (pour la génération des installeurs MSI).
  - Python 3.10+ (pour les scripts de maintenance et tests).

---

## 3. Arborescence du Dépôt

```
c:\Users\chris\VSCode\Evolution-WindowsPort\
├── VERSION                       # Numéro de version SemVer courante (ex: 0.9.7)
├── CHANGELOG.md                  # Historique des modifications (Keep a Changelog)
├── TODO.md                       # Feuille de route détaillée et statut des tâches
├── QUICKWINS.md                  # Tâches prioritaires à fort impact (toutes validées en 0.9.7)
├── TESTS.md                      # Guide et spécification de la suite de tests
│
├── winui/
│   ├── core/                     # Moteur C (evo-core.dll)
│   │   ├── evo-core.c            # Implémentation du moteur EDS / Camel
│   │   ├── evo-core.h            # Déclarations publiques et exports ABI
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
   - Travailler sur la branche active de la version en cours (actuellement `0.9.7`).
   - Réaliser **un commit atomique après chaque tâche traitée**.
   - Format de commit conventionnel : `feat(<scope>): <description en français>` ou `fix(<scope>): <description>`.
   - Toujours pousser (`git push origin <branche>`) après avoir validé un ensemble de tâches.

3. **Mise à jour documentaire continue** :
   - Dès qu'une tâche est achevée, cocher sa case dans `TODO.md` (`- [x]`).
   - Ajouter un paragraphe explicatif sous la section `## [<version>] — en cours` dans `CHANGELOG.md`.

---

## 6. Architecture & Modèles de Conception

### 6.1. Le Pont `evo-core` (C) <-> `CoreBridge` (C++/WinRT)
- Toutes les opérations d'E/S (réseau, disque, protocoles IMAP/SMTP) s'exécutent sur des threads d'arrière-plan dans `evo-core.c` via `core_submit()`.
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

## 7. État Actuel du Projet (Version 0.9.7)

### 7.1. Ce qui a été accompli récemment
- **QUICKWINS** : 100% des tâches du fichier `QUICKWINS.md` sont désormais réalisées et testées :
  - Exportation complète de dossier au format `.eml` via sélecteur de répertoire.
  - Recherche et filtrage instantané dans le Calendrier (titre, lieu, description).
  - Formatage localisé des tailles de fichiers et dates intelligentes.
  - Activation et désactivation de compte sans suppression dans les propriétés.
  - Bouton d'annulation d'urgence sur la barre de progression (`evo_core_cancel_operations`).
  - Quota du compte dans le ruban Fichier (`camel_folder_get_quota_info_sync`) avec jauge et nettoyage.
  - Autosave périodique (60s) et manuel (`Ctrl+S`) dans la fenêtre de rédaction (`ComposeWindow`).
  - Améliorations de l'installeur MSI (désactivation de Restart Manager, relance `LAUNCHAPP=1`).

### 7.2. Prochaine Étape pour l'Agent IA
- Poursuivre le traitement des tâches ouvertes de **[`TODO.md`](TODO.md)** par lots cohérents (créer, tester, commiter, mettre à jour le changelog et pousser sur `0.9.7`).
