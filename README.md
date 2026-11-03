<!-- cspell:disable -->
# Evolution pour Windows 11 (Portage natif MSVC & WinUI 3)

Ce dépôt contient le portage **100 % natif sous Windows 11** du client de messagerie, calendrier et carnet d'adresses open source **GNOME Evolution**, compilé avec **Microsoft Visual C++ (MSVC)** (sans MinGW, sans WSL et sans Cygwin) et doté d'une **interface utilisateur native WinUI 3 (`EvolutionWin`)**.

**Télécharger :** les installeurs MSI prêts à l'emploi (Windows 11 x64) sont publiés dans les [**Releases GitHub**](https://github.com/pitch7900/Evolution-WindowsPort/releases) — une Release par version, avec l'installeur `Evolution-<version>-x64.msi`, son empreinte SHA-256 (`.sha256`) et les notes du [CHANGELOG](./CHANGELOG.md). [**Dernière version**](https://github.com/pitch7900/Evolution-WindowsPort/releases/latest).

Pour le journal technique détaillé, l'historique des correctifs CMake/MSVC et le suivi des phases, consultez [**WINDOWS-PORT.md**](./WINDOWS-PORT.md).

---

## Projets GNOME d'origine

| Projet | Dépôt GitHub (miroir officiel) | Dépôt de développement | Version de base |
| --- | --- | --- | --- |
| Evolution | [github.com/GNOME/evolution](https://github.com/GNOME/evolution) | [gitlab.gnome.org/GNOME/evolution](https://gitlab.gnome.org/GNOME/evolution) | commit `be33fb6204` |
| Evolution Data Server | [github.com/GNOME/evolution-data-server](https://github.com/GNOME/evolution-data-server) | [gitlab.gnome.org/GNOME/evolution-data-server](https://gitlab.gnome.org/GNOME/evolution-data-server) | commit `ea6b0124d` |
| evolution-ews (Exchange / Microsoft 365) | [github.com/GNOME/evolution-ews](https://github.com/GNOME/evolution-ews) | [gitlab.gnome.org/GNOME/evolution-ews](https://gitlab.gnome.org/GNOME/evolution-ews) | commit `19cb82b3` |

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

## Principales fonctionnalités disponibles

EvolutionWin intègre l'ensemble des capacités éprouvées du moteur GNOME Evolution au sein d'une expérience utilisateur moderne et fluide, spécialement conçue pour Windows 11 :

### ✉️ Messagerie & Courrier
- **Comptes multiples et protocoles standards** : prise en charge complète des protocoles IMAP, POP3, SMTP et des comptes Microsoft 365 / Exchange Online via Microsoft Graph (`evolution-ews`).
- **Affichage moderne et sécurisé** : rendu HTML propulsé par Microsoft Edge WebView2, avec blocage automatique des images et contenus distants pour préserver la confidentialité, et bandeau de déblocage interactif.
- **Éditeur de rédaction enrichi** : rédaction en HTML ou texte brut, formatage riche, sélection de la police et de la taille par défaut, vérification orthographique native Windows, accusés de lecture (Disposition-Notification-To) et copie cachée automatique (Cci / Bcc).
- **Gestion intelligente des citations** : détection et personnalisation multilingue des préfixes de réponse et transfert (Re:, TR:, Fwd:, etc.) avec citation contextualisée selon la langue d'interface.
- **Gestionnaire de signatures** : signatures multiples avec attribution par compte et positionnement au-dessus ou en dessous de la citation.
- **Pièces jointes & Aperçu intégré** : barre d'accès rapide avec icônes par type MIME, aperçu intégré pour les images et documents texte, enregistrement et ouverture d'un clic.
- **Exportation et interopérabilité** :
  - **Export au format standard .eml** : enregistrement unitaire ou par lot avec sélecteur de fichier natif Windows (Ctrl+S).
  - **Glisser-déposer (Drag & Drop) vers Windows** : glissez un ou plusieurs courriels directement vers l'Explorateur Windows ou le Bureau.
- **Catégories visuelles** : attribution rapide de catégories colorées Fluent (10 teintes standards avec badges visuels dans la liste) synchronisées avec les drapeaux Camel.
- **Outils avancés** : visualiseur du flux brut RFC 822 / source MIME intégrale avec recherche instantanée (Ctrl+Maj+U), synthèse vocale native pour la lecture audio des courriels et impression native via WebView2.

### 📅 Calendrier & Agendas
- **Moteur éprouvé Evolution Data Server** : stockage haute performance et synchronisation locale et distante des événements.
- **Vues temporelles multiples** : bascule dynamique entre les vues Jour, Semaine de travail, Semaine complète et Mois.
- **Mini-calendrier latéral** : sélecteur de date mensuel dans le volet latéral avec commande ruban et option d'affichage/masquage dédiée.
- **Gestion du double fuseau horaire** : affichage simultané de deux fuseaux horaires configurables pour la coordination d'équipes distantes.
- **Rappels & Alertes** : système de notifications d'échéances avec possibilité d'activation ou de désactivation globale.

### 👥 Carnet d'adresses & Contacts
- **Carnets d'adresses multiples** : gestion centralisée des contacts locaux et d'entreprise.
- **Autocomplétion intelligente** : suggestions instantanées de destinataires dans les champs d'envoi (À, Cc, Cci).
- **Fiches complètes** : consultation et édition des coordonnées, adresses, téléphones et informations professionnelles.

### 📋 Tâches & Mémos
- **Suivi des activités** : gestion des tâches avec statuts, priorités et dates d'échéance.
- **Volet latéral To-Do Bar** : consultation et gestion rapide des tâches urgentes sans quitter la vue de messagerie.

### 💻 Expérience Windows 11 & Intégration Système
- **Design Fluent & Effet Mica** : interface moderne avec matériaux translucides Mica, coins arrondis et typographie Segoe UI Variable.
- **Thème système et personnalisation** : prise en charge complète des modes Clair et Sombre (automatique selon Windows ou forcé) et choix forcé de la langue d'interface.
- **Pastille de notification sur la barre des tâches** : compteur de messages non lus rendu en ARGB 32-bit haute définition avec transparence alpha parfaite et adaptation automatique à l'échelle DPI.
- **Notifications système Windows (Toasts)** : alertes de nouveau courrier personnalisables globalement, par compte ou par dossier, avec signal sonore commutable.
- **Sécurité et chiffrement DPAPI** : stockage protégé de tous les identifiants et mots de passe dans le Gestionnaire d'identifiants Windows.
- **Démarrage automatique** : option d'activation au lancement de Windows via le Registre utilisateur (HKCU\Software\Microsoft\\Windows\\CurrentVersion\\Run).
- **Aide et raccourcis clavier (F1)** : centre de documentation interactif des raccourcis clavier avec recherche instantanée et profils commutables (Outlook vs Evolution GNOME).
- **Surveillance réseau et santé** : barre d'état avec pastille dynamique et détection des anomalies de synchronisation.
- **Maintenance facilitée** : option de vidage automatique de la Corbeille à la fermeture de l'application.

---

## Organisation du dépôt

| Répertoire / Fichier | Rôle |
| --- | --- |
| [`WINDOWS-PORT.md`](./WINDOWS-PORT.md) | Documentation technique complète et suivi d'avancement du portage |
| [`CHANGELOG.md`](./CHANGELOG.md), [`VERSION`](./VERSION) | Évolutions par version ; version en cours (utilisée par l'installeur) |
| [`TODO.md`](./TODO.md) | Fonctionnalités restant à réaliser |
| [`winui/`](./winui) | Client natif Windows 11 : `winui/core` (`evo-core.dll`) et `winui/app` (`EvolutionWin.exe`) |
| [`tools/`](./tools) | Scripts de compilation MSVC (`build-*.bat`, `env.bat`, `check-prereqs.ps1`), couche CMake (`cmake/msvc-compat.cmake`), en-têtes POSIX de compatibilité (`compat/`), shim WebKit2 (`webkit-shim/`) et utilitaires (`tools/nuget.exe`) |
| `build/` | Sorties de compilation (`EvolutionWin.exe`, `evo-core.dll`, `evolution.exe`, `libexec/*.exe`, `build/intermediate/`), ignoré par Git |
| `dist/` | Installeurs MSI construits localement par `build-msi.bat`, ignoré par Git (versions publiées dans les [Releases](https://github.com/pitch7900/Evolution-WindowsPort/releases)) |
| [`evolution/`](./evolution) | Code source d'Evolution adapté pour MSVC |
| [`evolution-data-server/`](./evolution-data-server) | Code source d'Evolution Data Server adapté pour Windows 11 / MSVC |
| [`evolution-ews/`](./evolution-ews) | Code source d'evolution-ews (Microsoft 365 via Microsoft Graph, Exchange Web Services) compilé avec MSVC |
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
tools\build-ews.bat          :: evolution-ews (Microsoft 365 / Exchange Online)
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

### 4. Construire l'installeur MSI

```bat
tools\build-msi.bat          :: dist\Evolution-<version>-x64.msi (WiX v5, autonome)
msiexec /i dist\Evolution-<version>-x64.msi /qn   :: installation silencieuse (par machine)
```

### 5. Publier une version

```bat
tools\publish-release.bat -DryRun   :: vérifications et notes, sans rien publier
tools\publish-release.bat           :: étiquette vX.Y.Z + Release GitHub (MSI et .sha256 joints)
```

Le script vérifie que le MSI existe, que la section du [CHANGELOG](./CHANGELOG.md) est datée et que la Release n'existe pas encore ; `-PreRelease` (ou une version `x.y.z-suffixe`) publie une préversion. Jeton GitHub : `GH_TOKEN`, sinon les identifiants de Git.

---

## Licence

Ce projet est distribué sous la **GNU Lesser General Public License, version 2.1 ou (à votre choix) toute version ultérieure** (`LGPL-2.1-or-later`), dont le texte figure dans [`LICENSE`](./LICENSE). C'est la licence des projets GNOME d'origine :

- **Evolution** : LGPL v2 ou (à votre choix) LGPL v3, voir [`evolution/COPYING`](./evolution/COPYING). Quelques fichiers relèvent d'autres licences (OpenLDAP, MPL/GPL/LGPL), qui y sont listées.
- **Evolution Data Server** : LGPL v2 ou ultérieure, voir [`evolution-data-server/COPYING`](./evolution-data-server/COPYING).
- **evolution-ews** : LGPL v2.1 ou ultérieure, voir [`evolution-ews/COPYING`](./evolution-ews/COPYING).

Les fichiers repris de ces projets conservent leur licence et leurs mentions de copyright d'origine (en-têtes `SPDX-License-Identifier`).
