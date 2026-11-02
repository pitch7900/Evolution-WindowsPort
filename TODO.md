<!-- markdownlint-disable MD033 -- ancres <a id> utilisées par les liens des priorités et des doublons -->

# TODO

## Priorités pour une démo fonctionnelle

Hypothèse : démo de type « remplaçant d'Outlook » avec un compte Microsoft 365 et un compte IMAP. Critères : ce qui se voit en démo et ce qui risque de la faire échouer, au meilleur rapport effet / coût.

### Priorité 1 — corrections rapides et très visibles (≈ 1 à 2 semaines)

| #   | Tâche                                                                                                  | §   | Difficulté |
| --- | ------------------------------------------------------------------------------------------------------ | --- | ---------- |
| 1   | [Icône de la Boîte de réception, dossiers spéciaux en premier, noms traduits](#demo-dossiers)          | 9   | 🟢         |
| 2   | [Mode confidentiel des notifications](#demo-confidentiel) (utile pendant la démo elle-même)            | 19  | 🟢         |
| 3   | [Position du volet de lecture](#demo-volet-lecture) (droite / bas)                                     | 2   | 🟢         |
| 4   | [Nouveaux éléments](#demo-nouveaux-elements), [Archiver, Copier](#demo-archiver)                       | 7   | 🟢         |
| 5   | [Champ de recherche dans la barre de titre, Ctrl+E](#demo-recherche-titre)                             | 10  | 🟢         |
| 6   | Calendrier : [barre de navigation](#demo-cal-navigation), [couleur par calendrier](#demo-cal-couleurs) | 12  | 🟢         |
| 7   | [Écran de démarrage (splash screen)](#demo-splash) (retour visuel immédiat)                            | 1   | 🟢         |
| 8   | [Double-clic : message dans une fenêtre indépendante](#demo-fenetre-message)                           | 6   | 🟢         |
| 9   | [Portée de la recherche : dossier, sous-dossiers, compte, tous les comptes](#demo-recherche-portee)    | 10  | 🟢         |

### Priorité 2 — les gestes de base d'un utilisateur Outlook (≈ 3 à 5 semaines)

| #   | Tâche                                                                                                                                                                             | §   | Difficulté |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --- | ---------- |
| 10  | [Connexion Gmail compatible avec la double authentification](#demo-gmail-2fa) (bogue : « identifiants invalides ») — prérequis de tout compte Gmail                               | 3   | 🟡         |
| 11  | [Démarrage rapide à partir du cache local](#demo-cache-demarrage) (l'application rame à chaque ouverture)                                                                         | 4   | 🟢         |
| 12  | Calendrier et contacts du compte en ligne : [Microsoft 365 / Exchange Online](#demo-cal-contacts-m365) (🟡), [Gmail / Google Workspace](#demo-cal-contacts-gmail) (🟠)            | 3   | 🟡 / 🟠    |
| 13  | [Gestion simple du calendrier](#demo-gestion-calendrier) (créer, **modifier**, supprimer un événement) et [des contacts](#demo-gestion-contacts) (créer, **modifier**, supprimer) | 3   | 🟡         |
| 14  | [Autocomplétion des destinataires](#demo-autocompletion) — _fait en 0.9.4 et finitions Tab / « ; » / suppression récents_                                                         | 15  | 🟢 (Fait)  |
| 15  | [Simplifier l'assistant d'ajout de compte](#demo-assistant-compte)                                                                                                                | 3   | 🟡         |
| 16  | [Modifier un compte existant](#demo-modifier-compte)                                                                                                                              | 3   | 🟡         |
| 17  | [Indicateur d'état des connexions / hors connexion et _Se reconnecter_](#demo-etat-connexions)                                                                                    | 4   | 🟢         |
| 18  | [Barre de progression du rafraîchissement](#demo-progression)                                                                                                                     | 4   | 🟢         |
| 19  | [Répondre aux invitations reçues](#demo-invitations)                                                                                                                              | 13  | 🟡         |
| 20  | [Volet des tâches à droite](#demo-volet-taches) et [« Créer une tâche » depuis un mail](#demo-creer-tache)                                                                        | 16  | 🟡 (Fait)  |
| 21  | [Catégories communes en couleur](#demo-categories)                                                                                                                                | 17  | 🟡         |
| 22  | [Réorganisation de la page Paramètres](#demo-parametres)                                                                                                                          | 19  | 🟡         |

### Priorité 3 — ce qui fait vraiment « Outlook » (gros chantiers)

| #   | Tâche                                                                                                | §   | Difficulté |
| --- | ---------------------------------------------------------------------------------------------------- | --- | ---------- |
| 23  | [Grille calendrier Jour / Semaine](#demo-grille-calendrier) — _réalisé dans MainWindow.Calendar.cpp_ | 12  | 🔴 (Fait)  |
| 24  | [Éditeur d'événement complet](#demo-editeur-evenement) (périodicité, rappels)                        | 13  | 🟠         |
| 25  | [Grouper par discussion](#demo-conversations)                                                        | 5   | 🟠         |
| 26  | [Réponses automatiques](#demo-reponses-auto) Microsoft 365 / Exchange (si démo sur M365)             | 3   | 🟡         |
| 27  | [Carnet d'adresses global (GAL)](#demo-gal) (si démo sur M365)                                       | 15  | 🟡         |

**Après la démo** : aperçu des pièces jointes, règles, mode hors connexion complet, vues unifiées, traductions, import / export, mise à jour automatique, installation silencieuse avancée, nettoyage à la désinstallation, boîte Prioritaire, réunions avec invitations et Planification (🔴).

### Conventions

- Une tâche n'est cochable (`- [ ]`) qu'à **un seul endroit** ; ailleurs, un simple renvoi sans case à cocher (« cf. §n »).
- Drapeaux des messages : le **suivi** (libellé Urgent / À traiter… + échéance) et l'**importance** sont distincts (§7) ; les couleurs relèvent des **catégories** (§17).

## Légende des difficultés

| Niveau            | Signification                                                                                                        |
| ----------------- | -------------------------------------------------------------------------------------------------------------------- |
| 🟢 Facile         | Interface seule ou appel direct d'une fonction EDS / Camel existante — de quelques heures à 1 jour                   |
| 🟡 Moyenne        | Nouvelle fonction dans `evo-core` + interface, sans difficulté technique particulière — quelques jours               |
| 🟠 Difficile      | Nouveau composant, protocole spécifique (EWS, Graph, Sieve…) ou logique non triviale — 1 à 2 semaines                |
| 🔴 Très difficile | Nouvelle brique majeure (contrôle personnalisé complexe, moteur de règles, flux d'invitations…) — plusieurs semaines |

La difficulté tient compte de l'existant (cf. [WINDOWS-PORT.md](WINDOWS-PORT.md), phase 6b) : recherche dans le dossier courant, blocage du contenu distant dans WebView2, export `.eml`, sélection multiple, déplacement, drapeau, impression, vue calendrier mensuelle simple et liste de contacts sont déjà réalisés.

## Vue d'ensemble

| #   | Fonctionnalité                                   | Difficulté globale |
| --- | ------------------------------------------------ | ------------------ |
| 1   | Application et installation                      | 🟢                 |
| 2   | Interface générale et disposition                | 🟡                 |
| 3   | Comptes                                          | 🟠                 |
| 4   | Synchronisation et mode hors connexion           | 🟠                 |
| 5   | Mail – Liste des messages                        | 🟠                 |
| 6   | Mail – Lecture des messages                      | 🟢                 |
| 7   | Mail – Actions sur les messages                  | 🟠                 |
| 8   | Mail – Règles et courrier indésirable            | 🔴                 |
| 9   | Mail – Gestion des dossiers                      | 🟠                 |
| 10  | Recherche                                        | 🟡                 |
| 11  | Vues unifiées multi-comptes                      | 🟠                 |
| 12  | Calendrier – Vues et navigation                  | 🔴                 |
| 13  | Calendrier – Rendez-vous, réunions et ressources | 🔴                 |
| 14  | Calendrier – Calendriers partagés et externes    | 🟠                 |
| 15  | Contacts                                         | 🟡                 |
| 16  | Tâches                                           | 🟠                 |
| 17  | Catégories et codes couleur                      | 🟡                 |
| 18  | Import / Export                                  | 🟡                 |
| 19  | Configuration de l'application                   | 🟡                 |
| 20  | Traductions de l'interface                       | 🟠                 |

### Briques préalables (débloquent plusieurs fonctionnalités)

- **Barre d'outils à onglets façon ruban** (§2) : point d'entrée de la plupart des commandes Mail et Calendrier.
- **Progression et annulation des opérations** (§4) : réutilisées par la synchronisation, l'export, la recherche globale.
- **Dossiers virtuels Camel (`CamelVeeFolder`)** (§9) : dossiers de recherche, recherches enregistrées, boîte de réception unifiée, conversations multi-dossiers.
- **Grille calendrier Jour / Semaine / Mois** (§12) : préalable à toutes les vues calendrier avancées.
- **Éditeur d'événement complet** (§13) : préalable aux réunions, ressources, réunions en ligne.
- **Infrastructure de rappels** (§2, fenêtre des rappels) : utilisée par les rendez-vous, les tâches et le suivi des messages.
- **Catégories communes** (§17) : codes couleur partagés par les mails, les contacts, les événements et les tâches.

---

## 1. Application et installation — 🟢

- [x] 🟢 **Icône de l'exécutable** : fichier `.ico` multi-résolution (16, 24, 32, 48, 64, 256 px) intégré comme ressource dans `EvolutionWin.exe`
  - [x] Explorateur, barre des tâches, Alt+Tab, barre de titre (`AppWindow.SetIcon`)
  - [x] Raccourci du menu Démarrer (installeur MSI)
  - [x] _Paramètres › Applications installées_ (propriété `ARPPRODUCTICON` du MSI)
- [x] 🟢 **Écran de démarrage (splash screen)** : affichage immédiat d'une fenêtre de démarrage au lancement de l'application pendant le chargement des composants <a id="demo-splash"></a>
  - [x] Fenêtre épurée et centrée sans bordures (`OverlappedPresenter` sans barre de titre ni boutons système), avec l'icône / logo d'Evolution, le nom de l'application et la version (issue de [VERSION](VERSION))
  - [x] Indicateur d'activité discret (`ProgressBar` indéterminée ou `ProgressRing`) et message d'état sur l'étape en cours (« Démarrage des services… », « Chargement des comptes… »)
  - [x] Affichage instantané dès `App::OnLaunched` avant l'initialisation lourde du moteur `CoreBridge` et des services EDS, pour offrir un retour visuel immédiat
  - [x] Fermeture automatique et transition fluide vers la fenêtre principale (`MainWindow`) dès que l'interface est prête (événement `ready`)
  - [x] Prise en compte du thème sombre / clair de Windows pour l'arrière-plan du splash screen
- [x] 🟢 **Mise à jour du MSI : erreur 1618** « Une autre installation est en cours d'exécution » à l'installation de la 0.9.1 par-dessus la 0.9.0 — cause : dialogue « fichiers en cours d'utilisation » caché (EvolutionWin et services EDS ouverts), une seconde installation lancée entre-temps échouait ; le MSI ferme désormais ces programmes (`util:CloseApplication`)
- [ ] 🟡 **Option « installation silencieuse » du MSI avec fermeture forcée de l'application** — _existant : `util:CloseApplication` ferme déjà EvolutionWin et les services EDS / `gdbus` à chaque installation_
  - [ ] Propriété publique documentée (ex. `FORCECLOSE=1`), utilisable avec `/qn` ou `/passive` : `msiexec /i Evolution-<version>-x64.msi /qn FORCECLOSE=1` (déploiement GPO / Intune, mise à jour automatique) — _aujourd'hui `/qn` fonctionne déjà sans propriété : la fermeture est toujours forcée (vérifié sur la VM de test)_
  - [ ] Fermeture de **toutes les instances**, y compris dans les **autres sessions utilisateur** (serveur multi-sessions, session déconnectée) : demande de fermeture propre, puis arrêt forcé après un délai court — _fait pour la session courante (message de fermeture puis arrêt après 5 s) ; autres sessions non testées_
  - [ ] Fermeture de **toutes les dépendances** lancées par EvolutionWin (_fait : services EDS et `gdbus` ; reste : `camel-index-control`, `gspawn-win64-helper`, `msedgewebview2.exe`_) : services EDS (`evolution-source-registry`, fabriques carnet d'adresses / calendrier et leurs sous-processus), `gdbus`, `camel-index-control`, `gspawn-win64-helper`, processus `msedgewebview2.exe` **dont le dossier de données est celui d'EvolutionWin** (sans toucher à Edge ni aux autres applications WebView2)
  - [ ] Aucune boîte de dialogue « fichiers en cours d'utilisation » ni demande de redémarrage (`MSIRESTARTMANAGERCONTROL=Disable`, `REBOOT=ReallySuppress`) ; si un fichier reste verrouillé malgré tout, remplacement au prochain démarrage et code de sortie 3010 au lieu d'un échec — _fait : `RebootPrompt="no"` ; reste : `MSIRESTARTMANAGERCONTROL=Disable`, code 3010_
  - [ ] Sans `FORCECLOSE` en mode interactif : boîte de dialogue proposant de fermer Evolution — _aujourd'hui la fermeture est automatique, sans question_
  - [ ] Journalisation des processus fermés dans le journal MSI (`/l*v`) — _partiel : le journal contient l'action `Wix4CloseApplications` et la liste des programmes visés, pas le détail des processus réellement fermés_
  - [ ] Optionnel : relancer EvolutionWin pour l'utilisateur courant à la fin d'une installation silencieuse (`LAUNCHAPP=1`, utilisé par la mise à jour automatique)
- [ ] 🟡 **Nettoyage des données utilisateur à la désinstallation** (sur demande) — aujourd'hui la désinstallation ne retire que `C:\Program Files\Evolution` et laisse les données de chaque utilisateur
  - [ ] Données concernées : `%LOCALAPPDATA%\EvolutionWin\` (préférences, journaux, WebView2), `%LOCALAPPDATA%\evolution\` (comptes `sources\`, courrier local, carnets, agendas), cache du courrier `%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\`, identifiants `Evolution-Data-Server:*` du Gestionnaire d'identifiants Windows
  - [ ] **Conservées par défaut** (réinstallation sans perte, comme Outlook / Thunderbird) ; suppression seulement avec la propriété `PURGEUSERDATA=1` : `msiexec /x Evolution-<version>-x64.msi PURGEUSERDATA=1`
  - [ ] **Jamais lors d'une mise à jour** : une mise à jour majeure du MSI désinstalle l'ancienne version, le nettoyage doit être exclu dans ce cas (`REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE`)
  - [ ] Nettoyage du profil de l'utilisateur qui désinstalle (installation par machine : les autres profils ne sont pas accessibles proprement) ; documenter le nettoyage manuel des autres profils
  - [ ] Optionnel : case « Supprimer aussi mes comptes et données » dans l'interface de désinstallation du MSI (non affichée par _Paramètres › Applications installées_)
  - [ ] Journalisation des éléments supprimés dans le journal MSI
- [x] 🟢 **Menu « À propos »** (menu _Aide_ / _Fichier_, ou entrée en bas de la barre latérale) : boîte de dialogue avec icône, nom, version en cours, licence (LGPL 2.1 ou ultérieure), lien vers le dépôt GitHub et le [CHANGELOG.md](CHANGELOG.md)
  - [x] Version tirée du fichier [VERSION](VERSION) à la compilation (macro / ressource `VERSIONINFO` générée par MSBuild, comme le fait déjà `build-msi.ps1` pour le MSI), pour que l'exécutable, le MSI et la boîte « À propos » affichent toujours la même version
  - [x] Versions des composants (EDS, evolution-ews, Windows App SDK, WebView2) et bouton « Copier les informations » pour les rapports de bug
- [ ] 🟡 **Mise à jour automatique depuis GitHub** ([pitch7900/Evolution-WindowsPort](https://github.com/pitch7900/Evolution-WindowsPort))
  - [x] **Vérification de version** : dernière Release lue par l'API `https://api.github.com/repos/pitch7900/Evolution-WindowsPort/releases/latest` (champ `tag_name` pour la version, `assets` pour l'URL du MSI et de son `.sha256`), comparée à la version compilée dans l'exécutable (comparaison numérique `x.y.z`, pas textuelle) ; préversions via `releases` pour le canal « préversions »
  - [x] Fréquence : au démarrage (après quelques minutes, pour ne pas ralentir la relève) puis une fois par jour ; bouton _Rechercher les mises à jour_ dans la boîte « À propos »
  - [x] **Notification** de la nouvelle version (bandeau / toast) avec les nouveautés extraites du [CHANGELOG.md](CHANGELOG.md), boutons _Installer maintenant_, _Plus tard_, _Ignorer cette version_
  - [ ] **Téléchargement automatique** du MSI joint à la Release GitHub (`Evolution-<version>-x64.msi`) en arrière-plan, avec progression dans la barre d'état (§4), dans `%LOCALAPPDATA%\EvolutionWin\Updates`
  - [ ] **Vérification d'intégrité avant installation** : empreinte SHA-256 publiée à côté du MSI (`.sha256`) et signature Authenticode du MSI (`WinVerifyTrust`, éditeur attendu) ; refus d'installer si la vérification échoue
  - [ ] **Installation** : fermeture propre de l'application (boîte d'envoi vidée, synchronisation terminée), lancement de `msiexec /i … /passive` (élévation UAC, installation par machine), redémarrage automatique d'EvolutionWin à la fin
  - [ ] Nettoyage des MSI téléchargés après installation réussie
  - [ ] **Paramètres** (§19) : mise à jour automatique / notification seulement / désactivée ; canal (versions stables, préversions)
  - [ ] **Politique d'entreprise** : désactivation par clé de registre machine ou propriété MSI (`AUTOUPDATE=0`) pour les déploiements gérés (GPO / Intune)
  - [x] Évolution : passer aux _Releases_ GitHub (cf. point suivant) plutôt que `dist/` dans le dépôt
- [x] 🟡 **Publication des versions par les Releases GitHub** (au lieu de versionner les MSI dans `dist/`, ce qui alourdit l'historique Git à chaque version)
  - [x] Une _Release_ par version, avec l'étiquette Git `v<version>` (ex. `v0.9.3`) créée à partir du fichier [VERSION](VERSION)
  - [x] Fichiers joints à la Release : `Evolution-<version>-x64.msi` et son empreinte `.sha256`
  - [x] Notes de version reprises automatiquement de la section correspondante du [CHANGELOG.md](CHANGELOG.md)
  - [x] Script de publication (`tools\publish-release.ps1`, par l'API REST GitHub plutôt que `gh`, non installé ; jeton des identifiants de Git) lancé après `build-msi.bat` : vérifie que la version n'existe pas déjà, que le CHANGELOG contient la version, puis crée l'étiquette et la Release
  - [x] Préversions marquées _Pre-release_ (canal « préversions » de la mise à jour automatique)
  - [x] Retirer `dist/` du dépôt (et l'ajouter au `.gitignore`), mettre à jour la documentation ([WINDOWS-PORT.md](WINDOWS-PORT.md), README)
  - [-] 🟠 **Annulé** : construction et publication automatiques par GitHub Actions à chaque étiquette (nécessite un runner Windows avec toute la chaîne MSVC / gvsbuild, ou un runner auto-hébergé)

## 2. Interface générale et disposition — 🟡

### Structure de la fenêtre

- [x] 🟡 **Barre d'outils à onglets façon ruban** pour les vues Mail et Calendrier : _Fichier_, _Accueil_, _Envoi/réception_, _Dossier_, _Affichage_ (WinUI n'a pas de ruban : `TabView` / `SelectorBar` + `CommandBar`). Hors périmètre : intégrations tierces (Teams, OneNote, TeamViewer, Rainbow, Copilot)
- [x] 🟡 **Barre d'accès rapide** personnalisable dans la barre de titre (Envoyer/Recevoir, Annuler, etc.) — _reste : « Annuler » (pas encore de mécanisme d'annulation)_
- [x] 🟢 **Barre latérale gauche** de navigation entre modules (Mail, Calendrier, Contacts, Tâches) — `NavigationView` déjà en place, à restyler en barre d'icônes
- [x] 🟢 **Bandeau d'information** sous la barre d'outils (`InfoBar`), affiché dans les vues Mail et Calendrier (ex. réponses automatiques actives + bouton _Désactiver_)
- Bandeau de notification en bas de fenêtre (`StatusBar`) : supprimé au profit des Toasts système et du menu déroulant dans la barre d'état (cf. §19)

### Disposition des panneaux

- [x] 🟢 **Panneaux redimensionnables** : séparateurs déplaçables entre dossiers, liste des messages et volet de lecture ; tailles mémorisées entre deux sessions
  - [x] 🟢 **Visibilité et lisibilité des séparateurs (`PaneSplitter`)** : zone active de 8 px, trait visible de 1 px avec `DividerStrokeColorDefaultBrush`, épaississement à 3 px avec bord arrondi et couleur d'accentuation (`AccentFillColorDefaultBrush`) au survol et lors du glissement à la souris
- [ ] 🟡 **Position du volet de lecture** (menu _Affichage › Volet de lecture_), en Mail comme en Calendrier : <a id="demo-volet-lecture"></a> — _fait en Mail ; le Calendrier n'a pas encore de volet de lecture (§12)_
  - [x] 🟢 À droite
  - [x] 🟢 En bas — _hauteur liste / lecture fixe, pas encore réglable à la souris_
  - [x] 🟢 **Désactivé** (menu _Affichage › Volet de lecture_) : la liste de messages occupe toute la largeur disponible, mise à jour des boutons d'actions et ouverture des messages dans une fenêtre indépendante (MessageWindow) au double-clic ou avec la touche Entrée
  - [x] 🟢 Choix mémorisé dans les préférences
- [ ] 🟢 **Volet des dossiers** : normal / réduit / désactivé, en Mail comme en Calendrier (en Calendrier, le mode réduit garde le mini-calendrier)
  - [x] 🟢 **Décalage des compteurs de messages par rapport à la barre de défilement** : marge de 10 px à droite des badges de non-lus dans l'arborescence (`TreeViewItem` de `FolderTree`) afin d'éviter tout chevauchement avec la barre de défilement verticale (`ScrollBar`)
- [x] 🟢 **Espacement plus étroit** (mode compact) des listes, en Mail comme en Calendrier
- Barre des tâches à droite (To-Do bar) : cf. « Barre latérale des tâches », §16

### Fenêtres et lecture

- [ ] 🟡 **Ouvrir un événement ou un dossier dans une nouvelle fenêtre** (pour un message : fenêtre de lecture indépendante, §6) ; 🟢 **Fermer tous les éléments ouverts**
- [ ] 🟠 **Fenêtre des rappels** : rappels des rendez-vous (alarmes EDS) et des messages en suivi, avec Reporter / Ignorer
- [ ] 🟡 **Lecteur immersif** : vue de lecture plein écran épurée (taille du texte, espacement)
- [x] 🟢 **Lecture à voix haute** du message (`Windows.Media.SpeechSynthesis`)

## 3. Comptes — 🟠

### Ajout unifié avec détection automatique

- [x] 🟢 **Bouton générique « Ajouter un compte »** à la place de « Ajouter un compte IMAP/SMTP » (menu Réglages)
- [x] 🟠 **Détection automatique du type de compte** à partir de l'adresse email / du domaine :
  - [x] Microsoft 365 / Exchange Online (OAuth2 + Graph / EWS)
  - [x] Exchange on-premise (Autodiscover → EWS) — compte EWS (NTLM) créé, non testé faute de serveur
  - [x] Gmail / Google Workspace (OAuth2, IMAP/SMTP Google)
  - [x] IMAP/SMTP générique (repli par défaut : `imap.<domaine>` / `smtp.<domaine>`, signalés comme supposés)
- [x] **Mécanismes de détection** (dans l'ordre) :
  - [x] 🟢 Fournisseurs connus (gmail.com, outlook.com, etc.)
  - [x] 🟡 Enregistrements MX du domaine (`*.mail.protection.outlook.com`, `*.google.com`), via `DnsQuery`
  - [x] 🟡 Autodiscover Exchange / Microsoft 365
  - [x] 🟡 Autoconfig Thunderbird (`autoconfig.<domaine>`, base ISPDB)
  - [x] 🟢 Enregistrements DNS SRV (`_imaps._tcp`, `_submission._tcp`)
- [x] 🟢 **S'inspirer du dépôt Thunderbird [thunderbird/autoconfig](https://github.com/thunderbird/autoconfig)** pour aller plus vite : base ISPDB (fichiers XML de configuration par fournisseur : serveurs, ports, chiffrement, méthode d'authentification, OAuth2) et format `clientConfig` des fichiers autoconfig
  - [x] Réutiliser le format XML et le parseur pour les trois sources : fichiers embarqués, `autoconfig.<domaine>` / `.well-known/autoconfig`, ISPDB en ligne (`https://autoconfig.thunderbird.net/v1.1/<domaine>`)
  - [x] Embarquer une copie des fournisseurs les plus courants (détection hors ligne, remplace la liste « Fournisseurs connus »), mise à jour à chaque version
  - [x] Vérifier la licence du dépôt avant d'embarquer les fichiers et mentionner la provenance (MPL 2.0, `winui/core/ispdb/README.md`)
- [x] 🟡 **Confirmation / correction manuelle** : type détecté et paramètres proposés, possibilité de forcer un autre type ou de passer en configuration manuelle
- [x] 🟡 **Connexion Gmail / Google Workspace compatible avec la double authentification (2FA)** — _bogue résolu via OAuth2 RFC 8252 et repli mot de passe d'application_ <a id="demo-gmail-2fa"></a>
  - [x] Cause : Google refuse la connexion IMAP / SMTP par mot de passe du compte (« applications moins sécurisées » supprimées) ; seuls OAuth2 (XOAUTH2) ou un _mot de passe d'application_ fonctionnent.
  - [x] **OAuth2 par défaut pour Gmail**, sans champ mot de passe : flux moderne avec détection automatique ; la double authentification (SMS, application Google, clé de sécurité, passkey) est alors gérée par la page de connexion Google elle-même
  - [x] **Connexion dans le navigateur système** (redirection en boucle locale `http://127.0.0.1:<port>`, RFC 8252) et non dans un WebView2 intégré : évite le blocage Google `disallowed_useragent`
  - [x] **Client ID Google fourni avec l'application** (intégré via le service OAuth2 Google d'EDS avec portées Calendrier, Contacts et Mail)
  - [x] **Repli « mot de passe d'application »** (compte avec 2FA, OAuth2 indisponible) : champ dédié avec explication et lien vers `https://myaccount.google.com/apppasswords`
  - [x] Message d'erreur explicite si Google refuse le mot de passe et gestion des portées
  - [x] Jeton de rafraîchissement enregistré dans le Gestionnaire d'identifiants Windows et renouvelé automatiquement
- [x] 🟡 **Simplifier l'assistant d'ajout de compte** — écran épuré avec paramètres avancés sous Expander <a id="demo-assistant-compte"></a>
  - [x] **Écran principal réduit à l'essentiel** : adresse électronique, nom complet, mot de passe ou connexion moderne (OAuth2)
  - [x] **Résumé lisible de la détection** à la place des champs techniques avec type détecté et source
  - [x] **Section repliée « Paramètres avancés »** (`Expander`, fermé par défaut) regroupant : serveurs entrants / sortants, ports, chiffrement (TLS / STARTTLS en liste déroulante), nom d'utilisateur, URL EWS, paramètres OAuth2
  - [x] Section avancée **ouverte automatiquement** si le test échoue ou pour correction manuelle
  - [x] Nom du compte prérempli avec l'adresse
  - [x] **Test de connexion** avant création (test réseau et TLS IMAP / SMTP avec rapport d'état en direct)
  - [x] Libellés sans jargon et options claires
  - [ ] Une seule fenêtre à étapes (adresse → connexion → terminé) plutôt que deux boîtes de dialogue successives ; bouton _Précédent_

### Modifier un compte existant

- [x] 🟡 **Bouton _Modifier_** (et clic droit « Propriétés du compte… » dans l'arborescence) ouvrant la boîte de modification complète du compte <a id="demo-modifier-compte"></a>
- [x] 🟢 **Identité** : nom affiché, nom complet, adresse électronique, compte par défaut
- [x] 🟡 **Serveurs** : serveurs, ports, chiffrement, nom d'utilisateur de réception et d'envoi (modification des sources EDS `ESourceMailAccount` / `ESourceMailTransport` / `ESourceMailIdentity` et réémission des événements sans redémarrage)
- [x] 🟢 **Mot de passe** : champ de mise à jour du mot de passe dans le gestionnaire d'identifiants
- [x] 🟢 **Dossiers spéciaux** : attribution des rôles CAMEL*FOLDER_TYPE*\* (Envoyés, Brouillons, Corbeille, Indésirables, Archives) avec listes déroulantes basées sur les dossiers du compte
- [ ] 🟢 **Rédaction** : signature par défaut (nouveaux messages / réponses), copie cachée automatique, demande d'accusé de lecture
- [ ] 🟡 **Synchronisation** : intervalle de relève propre au compte (remplace la valeur globale des Paramètres, §19), dossiers synchronisés hors connexion et ancienneté (§4), compte activé / désactivé sans le supprimer
- [x] 🟡 **Test de connexion** après modification, avec bouton dédié et rapport d'état

### Boîtes partagées et comptes délégués (Mail, Calendrier, Contacts) — 🟠

En entreprise (Exchange Online / Microsoft 365, Exchange on-premise), les utilisateurs accèdent fréquemment à des boîtes aux lettres partagées (ex. support, info, boîtes de service), ainsi qu'aux calendriers et carnets d'adresses d'autres collaborateurs (délégation avec droits de lecture ou de gestion).

- [ ] 🟠 **Ajouter un compte ou dossier partagé en visibilité (clic droit sur un compte)** :
  - [ ] 🟡 **Menu contextuel du compte** : action _« Ouvrir le dossier / compte partagé d'un autre utilisateur… »_ (accessible par clic droit sur un compte dans l'arborescence Mail, Calendrier et Contacts).
  - [ ] 🟡 **Boîte de dialogue de sélection** : recherche de l'utilisateur ou de la boîte partagée par nom ou adresse email dans l'annuaire (GAL) ou saisie directe de l'adresse (ex. `equipe@entreprise.com` ou `collegue@entreprise.com`).
  - [ ] 🟠 **Courrier (Mail)** : ajout de la boîte aux lettres partagée / déléguée sous l'arborescence (accès aux dossiers Boîte de réception, Éléments envoyés, etc. selon les autorisations Exchange `FullAccess` / `SendAs` ou délégation Camel / EWS / Graph).
  - [ ] 🟡 **Calendrier** : ajout du calendrier partagé sous le groupe _Calendriers partagés_ ou _Autres calendriers_, avec synchronisation et affichage superposé ou côte à côte.
  - [ ] 🟡 **Contacts** : ajout du carnet d'adresses partagé de l'utilisateur sélectionné.
  - [ ] 🟢 **Mémorisation et suppression** : persistance des comptes et dossiers partagés ouverts entre les sessions ; option _« Masquer / Fermer le compte partagé »_ par clic droit.
  - Récupération des éléments supprimés du compte Exchange Online (Recoverable Items en self-service) : cf. §9

### Boîte d'archivage en ligne (Exchange Online Archiving / In-Place Archive) — 🟠

Dans Microsoft 365 / Exchange Online, les utilisateurs disposent souvent d'une boîte d'archivage distante séparée (_In-Place Archive_ ou _Archives en ligne_), distincte de la boîte aux lettres principale et de son quota, affichée sous forme d'un magasin de dossiers indépendant dans Outlook.

- [ ] 🟠 **Visibilité et accès à la boîte d'archivage en ligne (Online Archive)** :
  - [ ] 🟠 **Découverte automatique de l'archive en ligne** : détection de la boîte aux lettres d'archivage lors de la connexion Exchange Online / M365 (via Graph `GET /me/mailboxSettings` / `archiveMailbox` ou EWS Autodiscover `ArchiveDomain` / racine `archivemsgfolderroot`).
  - [ ] 🟡 **Affichage d'un nœud d'archive dédié dans l'arborescence** : afficher un compte / nœud distinct sous le compte principal, intitulé _« Archive en ligne – [Nom / Adresse] »_ avec une icône de boîte d'archives, développable pour parcourir ses dossiers.
  - [ ] 🟡 **Synchronisation et arborescence des dossiers archivés** : chargement et affichage des dossiers de l'archive (Boîte de réception archivée, dossiers personnalisés, éléments supprimés d'archive).
  - [ ] 🟡 **Consultation et recherche des messages archivés** : lecture complète des courriels archivés dans le volet de lecture et la fenêtre indépendante, inclusion dans la recherche transversale de messages (§10).
  - [ ] 🟡 **Déplacement vers l'archive** : déplacement ou copie de messages depuis la boîte principale vers les dossiers de l'archive en ligne via le menu _Déplacer vers_ ou par glisser-déposer.

### Compte par défaut

- [x] 🟢 **Désigner le compte par défaut** dans la liste des comptes, avec indication visuelle (`e_source_registry_set_default_mail_account`)
- [x] 🟢 Utilisé pour les nouveaux messages ; pour une réponse / un transfert, compte ayant reçu le message (repli sur le compte par défaut)
- [ ] 🟢 Calendrier et carnet d'adresses par défaut pour les nouveaux événements et contacts — _les nouveaux éléments vont déjà dans le calendrier / carnet par défaut d'EDS ; reste : les choisir dans l'interface_
- [ ] 🟡 Gestionnaire des liens `mailto:` (inscription d'EvolutionWin comme application de messagerie Windows). Facultatif au moment de l'installation MSI et dans les menus de configuration de l'application par la suite.

### Signatures — 🟡

- [x] 🟡 **Éditeur de signatures** (texte et HTML : mise en forme, images, liens) dans les Réglages — réutilise l'éditeur WebView2 de la rédaction ; sources EDS `ESourceMailSignature`
- [x] 🟢 Créer / modifier / supprimer plusieurs signatures nommées
- [x] 🟢 Par compte : signature des nouveaux messages et signature des réponses / transferts (ou aucune)
- [x] 🟡 Changement de signature dans la fenêtre de rédaction, mise à jour automatique au changement de compte expéditeur
- [x] 🟢 Placement (au-dessus / en dessous du texte cité)

### Réponses automatiques (Out of Office) — 🟠

- [ ] **Gestion des réponses automatiques** (_Fichier › Réponses automatiques_ et menu contextuel du compte) : <a id="demo-reponses-auto"></a>
  - [ ] 🟡 Exchange on-premise (EWS `GetUserOofSettings` / `SetUserOofSettings`, déjà présents dans evolution-ews)
  - [ ] 🟡 Microsoft 365 (Graph `mailboxSettings/automaticRepliesSetting`)
  - [ ] 🟠 Gmail / Google Workspace (API Gmail _vacation settings_, nouvelle portée OAuth2)
  - [ ] 🟠 IMAP avec Sieve / ManageSieve (extension `vacation` ; client ManageSieve à écrire)
  - [ ] 🟢 Option grisée pour les comptes qui ne la supportent pas
- [ ] **Options** :
  - [ ] 🟢 Activer / désactiver ; plage de dates ou durée indéterminée
  - [ ] 🟢 Messages distincts interne / externe (éditeur HTML)
  - [ ] 🟢 Externe : tous les expéditeurs ou seulement les contacts
  - [ ] 🟡 Exchange / M365 : bloquer le calendrier, refuser les nouvelles invitations, annuler les réunions de la période
- [ ] 🟢 **Indicateur visible** : bandeau « Réponses automatiques activées pour ce compte » + _Désactiver_ ; rappel à l'ouverture si la période est expirée

### Informations du compte (onglet Fichier)

- [ ] 🟡 Quota du compte (`camel_folder_get_quota_info_sync`) et outils de nettoyage de la boîte aux lettres
- Paramètres du compte : cf. « Modifier un compte existant » ci-dessus ; options générales : cf. §19

### Calendrier et contacts des comptes Microsoft 365 et Gmail

Objectif : retrouver dans EvolutionWin le calendrier et les contacts du compte en ligne de l'utilisateur, et pouvoir les gérer simplement (les fonctions avancées restent aux §12 à §15). Aujourd'hui : un compte Microsoft 365 crée déjà une collection EDS avec calendrier et contacts activés (`e_source_collection_set_calendar_enabled` / `_contacts_enabled`), sans validation de bout en bout ; un compte Gmail n'est créé qu'en IMAP / SMTP (XOAUTH2), sans calendrier ni contacts ; l'interface sait lister, créer et supprimer des événements et des contacts, mais pas les modifier.

- [ ] 🟡 **Microsoft 365 / Exchange Online** : vérifier de bout en bout que la collection `microsoft365` (Graph) découvre et synchronise les calendriers et carnets d'adresses du compte ; les afficher sous le nom du compte dans les vues Calendrier et Contacts <a id="demo-cal-contacts-m365"></a>
  - [x] Portées OAuth2 nécessaires (`Calendars.ReadWrite`, `Contacts.ReadWrite`) demandées à la connexion ; consentement manquant → message clair et _Se reconnecter_
  - [ ] Exchange on-premise (collection `ews`) : même vérification dès qu'un serveur de test est disponible
- [ ] 🟠 **Gmail / Google Workspace** : à l'ajout d'un compte Gmail, créer aussi une collection Google (backend `google` d'EDS : calendriers CalDAV, contacts CardDAV / Google People) avec le même jeton OAuth2 <a id="demo-cal-contacts-gmail"></a>
  - [x] Portées OAuth2 Calendar et Contacts ajoutées à la connexion Google (en plus de la messagerie)
  - [ ] Comptes Gmail déjà configurés : bouton _Activer le calendrier et les contacts_ dans la page du compte (nouvelle connexion pour accorder les portées)
- [ ] 🟡 **Gestion simple du calendrier du compte** : <a id="demo-gestion-calendrier"></a>
  - [x] Événements du calendrier principal (et des autres calendriers du compte) affichés avec la couleur du calendrier
  - [x] Créer un événement dans le calendrier choisi (calendrier principal du compte par défaut) : titre, lieu, date, heures ou journée entière, description
  - [x] **Modifier** un événement existant (manquant aujourd'hui) et le supprimer, avec envoi au serveur — _clic sur l'événement : fenêtre modifiable (lecture seule si l'agenda l'est), organisateur et participants_
  - [ ] Modifications faites ailleurs (Outlook Web, Google Agenda, téléphone) visibles à la synchronisation suivante ; bouton _Actualiser_
  - [x] Événements périodiques existants affichés correctement (occurrences développées), en lecture seule tant que l'éditeur complet (§13) n'existe pas
- [ ] 🟡 **Gestion simple des contacts du compte** : <a id="demo-gestion-contacts"></a>
  - [x] Contacts du compte (et sous-dossiers / groupes de contacts) listés avec le nom du carnet d'origine — _volet « Carnets d'adresses » par compte, cases à cocher mémorisées, carnet actif_
  - [x] Créer un contact dans le carnet choisi (carnet du compte par défaut) : nom, prénom, adresses email, téléphones, société, fonction, notes
  - [ ] **Modifier** un contact existant (manquant aujourd'hui) et le supprimer, avec envoi au serveur
  - [ ] Contacts utilisables dans l'autocomplétion des destinataires (§15)
- [ ] 🟢 Erreurs de synchronisation du calendrier / des contacts remontées dans l'indicateur d'état des connexions (§4)

## 4. Synchronisation et mode hors connexion — 🟠

### Progression

- [x] 🟢 **Barre de progression dans la barre d'état** lors d'un rafraîchissement (messages de progression `CamelOperation` relayés par `evo-core`) : <a id="demo-progression"></a>
  - [x] Texte de l'opération en cours (« Compte X – Boîte de réception : chargement des en-têtes 120 / 850 »)
  - [x] Pourcentage quand le total est connu, indéterminée sinon
  - [x] Progression globale pour plusieurs comptes / dossiers en parallèle
  - [ ] 🟡 Bouton _Annuler_ / _Annuler tout_ (`GCancellable`)
  - [x] Masquage automatique à la fin, message bref en cas d'erreur
- [x] 🟢 **Progression sur l'icône de la barre des tâches Windows** (`ITaskbarList3`, rouge en cas d'erreur)

### Envoi et réception

- [x] 🟢 **Envoyer/Recevoir tous les dossiers** (F9 ; F5 existe déjà) et **Mettre à jour le dossier courant** (Maj+F9)
- [x] 🟢 **Boîte d'envoi et Envoyer tout** : messages mis en file d'attente au lieu d'être envoyés directement (prérequis du mode hors connexion)
  - [x] Dossier local _Boîte d'envoi_ (stockage local Camel), visible dans l'arborescence avec le nombre de messages en attente
  - [x] Envoi automatique de la file dès que le réseau revient (et à chaque relève), dans l'ordre de création
  - [ ] Message en attente modifiable ou supprimable tant qu'il n'est pas parti ; envoi différé (« Envoyer à… ») optionnel
  - [x] Erreurs d'envoi : message conservé dans la boîte d'envoi avec l'erreur affichée, nouvelle tentative automatique
- [ ] 🟠 **Groupes d'envoi/réception** : groupes de comptes / dossiers avec planification (intervalle, à la fermeture, hors connexion)

### Hors connexion et téléchargement

- [x] 🟢 **Démarrage rapide à partir du cache local** — _constat : l'application rame à chaque ouverture ; `core_list_folders_run` attend la connexion au serveur (`core_ensure_connected`) puis interroge le serveur (`camel_store_get_folder_info_sync` sans `CAMEL_STORE_FOLDER_INFO_FAST`) avant d'afficher l'arborescence, alors que Camel conserve déjà sur disque l'arborescence et les résumés de messages_ <a id="demo-cache-demarrage"></a>
  - [ ] **Mesurer d'abord** : journal horodaté des étapes du démarrage (lancement des services EDS / bus D-Bus, `ESourceRegistry`, connexion de chaque compte, liste des dossiers, première liste de messages, premier rendu) pour cibler les vraies lenteurs
  - [x] **Afficher immédiatement depuis le cache**, sans attendre le réseau :
    - [x] Arborescence des dossiers et compteurs de non-lus lus localement (`CAMEL_STORE_FOLDER_INFO_FAST`, magasin hors ligne), sans connexion préalable
    - [x] Liste des messages du dernier dossier ouvert lue dans le résumé local (`CamelFolderSummary`) ; message sélectionné relu depuis le cache de messages
    - [x] Restauration de l'état de la dernière session (module, compte, dossier, message sélectionné, tri, défilement)
  - [x] **Puis synchroniser en arrière-plan** : connexion des comptes en parallèle (pas l'un après l'autre), rafraîchissement des dossiers et de la liste par événements (`folders-updated`, `folder-changed`), progression dans la barre d'état (§4) ; l'interface reste utilisable pendant ce temps
  - [ ] **Cache propre à EvolutionWin** pour ce que Camel / EDS ne conservent pas : rôles des dossiers, ordre personnalisé, dernière liste affichée, aperçus de messages (§5), éventuellement un instantané JSON de l'arborescence par compte, invalidé à la synchronisation
  - [ ] Calendrier, contacts et tâches : affichage depuis les caches EDS des backends (`ECalCache` / `EBookCache`) avant la fin de la synchronisation
  - [ ] Démarrage des services EDS (`evolution-source-registry`, fabriques) en parallèle de la création de la fenêtre ; fenêtre affichée sans attendre la fin du démarrage du moteur (`core_wait_started`) (cf. écran de démarrage, §1)
  - [ ] Objectif mesurable : fenêtre et contenu du dernier dossier visibles en moins de 2 s avec un cache existant, quel que soit l'état du réseau
- [ ] 🟠 **Cache local complet pour le mode hors connexion** : messages (en-têtes, corps et pièces jointes), contacts, calendriers et tâches disponibles sans réseau
  - [ ] Synchronisation hors connexion des dossiers (réglage IMAP `stay-synchronized` / `sync-offline` de Camel), par compte et par dossier, avec limite d'ancienneté (ex. 3 derniers mois) et de taille
  - [ ] Caches EDS des carnets d'adresses et calendriers (backends en cache local : EWS, CalDAV, CardDAV) consultables hors ligne
  - [ ] Recherche, lecture, aperçu des pièces jointes et rédaction fonctionnels hors ligne
  - [ ] Actions faites hors ligne (lu / non lu, drapeau, déplacement, suppression, événements, contacts) journalisées et rejouées au retour du réseau ; résolution des conflits
- [ ] 🟠 **Travailler en mode hors connexion** : bascule manuelle globale (`CamelOfflineStore`) ; envois mis en file d'attente dans la boîte d'envoi
- [ ] 🟡 **Détection automatique de l'état du réseau** : surveillance de la connectivité (`GNetworkMonitor` / `NetworkInformation.NetworkStatusChanged`) et vérification régulière de l'accès aux serveurs des comptes (ex. toutes les 30 s hors ligne) ; passage automatique hors ligne / en ligne, puis relève et envoi de la boîte d'envoi au retour
- [x] 🟢 **Indicateur d'état des connexions** dans la barre d'état (en bas), qui sert aussi d'indicateur du mode hors connexion — icône verte : tout est connecté ; orange : connexion en cours / partielle ; rouge : erreur ; grise : hors connexion <a id="demo-etat-connexions"></a>
  - [x] 🟢 Hors connexion : message « Mode hors connexion », bouton _Travailler hors connexion_ / _Se connecter_
  - [x] Clic : panneau listant chaque compte et chacun de ses services (IMAP, SMTP, Exchange / EWS, Microsoft Graph, Gmail, CalDAV / CardDAV…) avec état, serveur, dernière synchronisation réussie
  - [x] Message d'erreur explicite en cas d'échec : serveur injoignable, certificat refusé, délai dépassé, quota, **authentification requise**
  - [x] **Réauthentification** mise en évidence (mot de passe changé, jeton OAuth2 expiré ou révoqué, changement de compte / de tenant, MFA) : bouton _Se reconnecter_ qui relance la saisie du mot de passe ou la connexion OAuth2 sans passer par les Paramètres
  - [x] Bouton _Réessayer_ par compte ; lien vers les paramètres du compte
  - [ ] Pastille d'erreur sur le compte concerné dans l'arborescence des dossiers et notification Windows (une seule fois par erreur) quand une réauthentification est nécessaire
  - [x] État fourni par `evo-core` (événement `connection-status` par service, à partir de `CamelService:connection-status`, des erreurs de relève et de l'état des sources EDS)
  - Menu déroulant des notifications dans la barre d'état (à côté de l'indicateur d'état des connexions) : cf. §19
- [ ] 🟡 **Préférences de téléchargement** : en-têtes seuls / message complet, limite de taille (synchronisation hors connexion des dossiers : cf. « Cache local » ci-dessus)
- [ ] 🟠 **Mode en-têtes seuls** : télécharger les en-têtes, marquer pour téléchargement (message / copie), annuler les marques, traiter les en-têtes marqués

## 5. Mail – Liste des messages — 🟠

### Tri et colonnes

- [x] 🟡 **Trier les emails** via la ligne « Trier par » au-dessus de la liste et _Affichage › Trier par_ (la `ListView` actuelle n'a pas d'en-têtes de colonnes) :
  - [x] Date, Expéditeur, Destinataire, Objet, Taille, Pièces jointes, Indicateur, Catégories, Lu / non lu
  - [ ] Importance (absente des résumés Camel), Compte et Type (sans vue unifiée), date de début / d'échéance de l'indicateur
  - [x] 🟢 Inverser l'ordre du tri pour chaque critère
  - [x] 🟢 Mémoriser le tri par dossier
- [ ] 🟡 **Regroupement** par critère de tri (Aujourd'hui, Hier, La semaine dernière…), avec Développer / Réduire les groupes
- [ ] 🟡 **Ajouter / retirer des colonnes**
- [ ] 🟡 **Changer l'affichage** : Compact, Unique, Aperçu ; 🟢 Rétablir l'affichage par défaut
- [ ] 🟡 **Paramètres d'affichage avancés** : polices, tailles (mise en forme conditionnelle : cf. §17)
- [ ] 🟡 **Aperçu du message** : désactivé, 1, 2 ou 3 lignes sous l'objet (extrait du corps à stocker dans le résumé)

### Barre de lecture rapide

- [x] 🟢 **Barre verticale à gauche de chaque message** (comme Outlook) : affichée en couleur d'accent pour les messages non lus, invisible pour les lus
  - [x] Clic sur la barre : bascule Lu / Non lu sans ouvrir ni sélectionner le message (fonction `evo-core` existante)
  - [x] Survol : barre mise en évidence et info-bulle « Marquer comme lu » / « Marquer comme non lu »
  - [ ] Fonctionne aussi sur un en-tête de conversation (marque tout le fil) et dans les vues unifiées

### Filtres rapides

- [x] 🟢 **Filtres rapides** au-dessus de la liste (boutons bascule, comme dans Outlook) :
  - [x] Tous / Non lus / Lus
  - [ ] Avec indicateur, avec pièces jointes, cette semaine, importants, mentionné — _fait : indicateur, pièces jointes, cette semaine ; reste : importants et mentionné (importance et destinataires absents des résumés transmis par `evo-core`)_
  - [ ] Combinables avec la recherche et fonctionnels dans les vues unifiées — _fait : combinés avec la recherche ; reste : vues unifiées (§11)_

### Conversations

- [ ] 🟠 **Grouper par discussion** (_Affichage › Afficher sous forme de conversations_) : <a id="demo-conversations"></a>
  - [ ] Regroupement par `Message-ID` / `In-Reply-To` / `References` (`CamelFolderThread`), repli sur l'objet normalisé « RE: / TR: »
  - [ ] Fil réductible / développable, nombre de messages et de non lus
  - [ ] 🟠 Inclure les messages d'autres dossiers (réponses envoyées)
  - [ ] 🟢 Choix mémorisé par dossier
- [ ] 🟡 **Paramètres de conversation** : expéditeurs au-dessus de l'objet, toujours développer la conversation sélectionnée, retrait classique

### Boîte de réception Prioritaire

- [ ] 🟠 **Onglets _Prioritaire_ / _Autres_** (classement Exchange Online `inferenceClassification` via Graph ; sans objet pour les autres comptes)

## 6. Mail – Lecture des messages — 🟢

Le contenu distant est déjà bloqué par le lecteur WebView2 (`WebResourceRequested` → 403).

- [x] 🟢 **Bandeau « contenu externe bloqué »** au-dessus du message :
  - [x] « Afficher les images pour ce message » (une seule fois)
  - [x] « Toujours faire confiance à l'expéditeur »
  - [x] « Toujours faire confiance au domaine » (ex. `@exemple.ch`)
- [x] 🟢 **Liste des expéditeurs / domaines de confiance** gérée dans les préférences
- [x] 🟢 **Option globale** : jamais / expéditeurs de confiance seulement / toujours charger le contenu externe
- [x] 🟢 **Enregistrer sous** (`.eml`, fonction `evo-core` existante), **Enregistrer toutes les pièces jointes**
- [ ] 🟡 **Impression** : aperçu et styles mémo / tableau (l'impression simple existe déjà)
- [ ] 🟠 **Aperçu des pièces jointes** directement dans le volet de lecture (et dans la fenêtre indépendante), sans les enregistrer ni les ouvrir dans une application externe ; même fonction pour les pièces jointes des événements du calendrier (§13) et des tâches (§16)
  - [ ] Clic sur une pièce jointe → aperçu à la place du corps du message, bouton _Retour au message_, navigation entre pièces jointes
  - [ ] **Rendu sécurisé, sans exécution de code actif** :
    - [ ] 🟢 Images (PNG, JPEG, GIF, BMP, WebP, SVG converti en image fixe) : décodage natif (`BitmapImage` / WIC), SVG sans scripts
    - [ ] 🟢 Texte, CSV, JSON, XML, code : affichage en texte brut (jamais interprété)
    - [ ] 🟡 PDF : rendu page par page en images avec `Windows.Data.Pdf` (aucun JavaScript, formulaire, lien de lancement ni pièce jointe imbriquée exécutés), zoom et défilement
    - [ ] 🟡 HTML / EML / MSG joint : WebView2 isolé avec scripts désactivés et contenu distant bloqué (même règles que le lecteur, §6)
    - [ ] 🟠 Office (Word, Excel, PowerPoint, OpenDocument) : rendu en lecture seule **sans macros ni contenu actif** — conversion en PDF / image hors processus, ou gestionnaires d'aperçu Windows (`IPreviewHandler`) exécutés dans un processus isolé à faibles privilèges ; macros VBA, objets OLE / ActiveX, liens externes et DDE jamais exécutés
    - [ ] 🟢 Types non pris en charge ou à risque (`.exe`, `.js`, `.vbs`, `.ps1`, `.lnk`, `.hta`, archives…) : pas d'aperçu, simple fiche (nom, type, taille) avec avertissement
  - [ ] 🟡 Fichiers d'aperçu temporaires dans un dossier privé, supprimés à la fermeture ; taille maximale configurable pour l'aperçu
  - [ ] 🟢 Boutons _Enregistrer_ et _Ouvrir avec l'application_ (avec avertissement si le fichier vient d'Internet : marque _Mark of the Web_ `Zone.Identifier` posée à l'enregistrement)
- [x] 🟢 **Double-clic sur un mail : ouverture dans une fenêtre indépendante** (aussi avec Entrée et _Ouvrir_ du menu contextuel) <a id="demo-fenetre-message"></a>
  - [x] Fenêtre de lecture avec en-têtes, pièces jointes, bandeau de sécurité / contenu externe et barre d'actions (Répondre, Répondre à tous, Transférer, Supprimer, Déplacer, Lu / Non lu, Imprimer)
  - [x] Navigation message précédent / suivant dans la fenêtre
  - [x] Plusieurs fenêtres ouvertes simultanément ; taille et position mémorisées
  - [x] Brouillon (dossier Brouillons) : double-clic ouvre la fenêtre de rédaction pour reprendre le message
  - [x] Message marqué comme lu à l'ouverture
- [ ] 🟡 **Affichage du contenu brut et des en-têtes détaillés (source MIME / Propriétés du message)** :
  - [ ] **Accès rapide** : clic droit › _Propriétés_ sur un mail dans la liste des messages, raccourci clavier (ex. Ctrl+U ou Alt+Entrée), et commande dédiée dans le volet de lecture et la fenêtre indépendante
  - [ ] **En-têtes Internet complets** : affichage exhaustif de la totalité des en-têtes RFC 5322 bruts (`Received`, `Authentication-Results`, `DKIM-Signature`, `SPF`, `Message-ID`, en-têtes `X-*`), avec sélection, recherche et bouton de copie rapide vers le presse-papiers (équivalent des « Options du message / Propriétés » d'Outlook)
  - [ ] **Contenu complet brut (source MIME / RFC 822)** : visualiseur du flux brut intégral non interprété du courriel (en-têtes et corps multi-parties avec frontières MIME, codages Base64 / Quoted-Printable d'origine) avec police monospace, recherche textuelle (Ctrl+F) et boutons de copie / export `.eml`
  - [ ] **Métadonnées techniques de diagnostic** : taille exacte en octets, compte et dossier d'origine, UID Camel / IMAP, encodage des caractères (charset), état de synchronisation

## 7. Mail – Actions sur les messages — 🟠

### Créer et répondre

- [x] 🟢 **Nouveaux éléments** (menu) : courrier, rendez-vous, réunion, contact, tâche <a id="demo-nouveaux-elements"></a> — _« Réunion » grisée en attendant les invitations (§13)_
- [ ] 🟡 **Brouillon en enregistrement automatique (autosave)** pendant la rédaction (nouveau message, réponse, transfert) : l'utilisateur n'a jamais à penser à enregistrer — _existant : le dossier Brouillons est déjà reconnu (rôle `CAMEL_FOLDER_TYPE_DRAFTS` ou dossier configuré dans l'identité EDS) ; aucun enregistrement de brouillon aujourd'hui_
  - [ ] **Autosave** : premier enregistrement dès que le message est modifié (destinataire, objet ou texte saisi), puis à chaque pause de frappe (ex. 3 s sans saisie), au plus tard toutes les 30 s pendant une saisie continue, à chaque ajout / retrait de pièce jointe, à la perte de focus et à la fermeture de la fenêtre ; aucun enregistrement si rien n'a changé depuis le précédent
  - [ ] Enregistrement en arrière-plan, sans bloquer la saisie ni voler le focus ; échec (serveur injoignable) signalé discrètement et nouvelle tentative au cycle suivant
  - [ ] Ctrl+S reste disponible pour forcer l'enregistrement immédiat
  - [ ] Brouillon écrit dans le dossier **Brouillons du compte expéditeur** (`evo_core_save_draft` : message MIME complet avec pièces jointes, en-têtes `In-Reply-To` / `References` conservés, indicateur `CAMEL_MESSAGE_DRAFT`) ; chaque enregistrement **remplace** la version précédente (pas de doublons)
  - [ ] **Création du dossier Brouillons** s'il n'existe pas encore sur le serveur (`camel_store_create_folder_sync`), puis inscription dans l'identité du compte (`e_source_mail_composition_set_drafts_folder`) ; repli sur un dossier Brouillons local si le serveur refuse ou si l'on est hors connexion, avec transfert au retour du réseau (§4)
  - [ ] Changement de compte expéditeur pendant la rédaction : brouillon déplacé vers le dossier Brouillons du nouveau compte
  - [ ] À l'envoi : brouillon supprimé du dossier Brouillons ; à la fermeture sans envoi : **pas de question**, le brouillon est déjà enregistré (dernier autosave à la fermeture) ; bouton _Supprimer le brouillon_ dans la fenêtre de rédaction pour abandonner le message
  - [ ] Reprise d'un brouillon : double-clic dans le dossier Brouillons → fenêtre de rédaction pré-remplie (cf. §6), compte, signature et format (HTML / texte) restaurés
  - [ ] Indication discrète de l'état de l'autosave dans la fenêtre de rédaction (« Enregistrement… », « Brouillon enregistré à 14:32 », « Non enregistré — nouvelle tentative »)
  - [ ] Récupération après plantage : copie locale écrite à chaque autosave, brouillons non synchronisés retrouvés au redémarrage
- [ ] 🟡 **Répondre par une réunion** (réunion pré-remplie avec les participants) — dépend de §13
- [x] 🟢 **Transférer en pièce jointe** pour un message seul (déjà fait en sélection multiple)

### Supprimer, archiver, déplacer

- [x] 🟢 **Archiver** : déplacer vers le dossier d'archive du compte (réglage `archive-folder` d'EDS), touche Retour arrière <a id="demo-archiver"></a> — _dossier Archive du compte (rôle détecté), créé au premier usage s'il n'existe pas_
- [x] 🟢 **Copier vers un dossier** ; liste des derniers dossiers utilisés dans _Déplacer_
- [ ] 🟠 **Ignorer la conversation** : les nouveaux messages du fil vont directement à la Corbeille
- [ ] 🟠 **Nettoyer la conversation** : supprimer les messages redondants (entièrement cités dans un message plus récent)

### Indicateurs et catégories

Trois notions distinctes, comme dans Outlook : le **suivi** (drapeau avec libellé et échéance : ce que _je_ dois faire), l'**importance** (fixée par l'expéditeur) et les **catégories** (classement en couleur, §17).

- [ ] 🟡 **Suivi des messages** (évolution du drapeau simple existant) :
  - [ ] **Libellé du suivi** : Urgent, À traiter, À répondre, Pour information, En attente, et libellés personnalisés
  - [ ] **Échéance** : aujourd'hui, demain, cette semaine, semaine prochaine, sans date, date personnalisée
  - [ ] Choix depuis le menu contextuel, le ruban et des raccourcis clavier
  - [ ] Icône et couleur distinctes dans la liste des messages (ex. rouge pour Urgent, orange pour À traiter, échéance dépassée en rouge) et libellé + échéance dans l'en-tête du volet de lecture
  - [ ] Stockage dans les indicateurs Camel (`follow-up`, `due-by`, `completed-on`), synchronisé avec le serveur (mots-clés IMAP, `flag` Exchange / Graph)
  - [ ] _Marquer comme terminé_ en un clic (avec date de fin), _Effacer l'indicateur_
  - [ ] Filtres rapides « Urgents » et « À traiter » (§5) et dossiers de recherche correspondants (§9)
  - [ ] 🟠 Rappel sur un suivi — dépend de la fenêtre des rappels (§2)
- [ ] 🟡 **Importance** (haute / normale / basse) : affichée dans la liste et le volet de lecture (en-têtes `Importance` / `X-Priority`), choisie à la rédaction ; filtre rapide « Importants » (§5)
- Catégoriser le message : cf. §17 ; créer une tâche à partir du message : cf. §16 ; export par glisser-déposer vers l'extérieur : cf. §18 ; propriétés et contenu brut du message : cf. §6

### Actions rapides

- [ ] 🟠 **Actions rapides** en un clic, personnalisables (déplacer + marquer comme lu, transférer à un responsable, message d'équipe…), avec éditeur d'actions

### Groupes Microsoft 365

- [ ] 🟠 **Groupes Microsoft 365** (Graph, comptes M365) : Nouveau groupe, Parcourir les groupes ; boîte aux lettres **et** calendrier de groupe (le calendrier apparaît aussi dans la liste des calendriers, §14)

## 8. Mail – Règles et courrier indésirable — 🔴

- [ ] 🔴 **Règles côté client** : éditeur de règles (conditions / actions), exécution à la réception (`CamelFilterDriver`)
  - [ ] 🟡 Créer une règle à partir du message sélectionné (une fois l'éditeur en place)
  - [ ] 🟡 Exécuter les règles maintenant sur un dossier
- [ ] 🔴 **Règles côté serveur** : Exchange (EWS / Graph `messageRules`), Sieve (ManageSieve)
- [ ] 🟡 **Courrier indésirable** : marquer comme indésirable / non indésirable (déplacement vers / depuis le dossier Indésirables)
- [ ] 🟡 **Listes d'expéditeurs** bloqués / approuvés (expéditeur ou domaine), options du filtre ; 🟠 synchronisées avec Exchange
- [ ] 🟡 **Signaler** un message comme hameçonnage / spam (transfert en pièce jointe vers une adresse configurable, ou API du fournisseur)

## 9. Mail – Gestion des dossiers — 🟠

- [ ] 🟡 **Copier / déplacer des mails et des dossiers d'un compte à un autre** (ex. d'un compte IMAP vers Microsoft 365, d'un ancien compte vers le nouveau) — _existant : le moteur accepte déjà un compte de destination pour un message (`core_move_message_run`, `camel_folder_transfer_messages_to_sync`) mais l'interface ne propose que les dossiers du même compte ; copie / déplacement de dossier limités au même compte, sans sous-dossiers_
  - [ ] **Unitairement** : menus _Déplacer vers_ / _Copier vers_ listant les dossiers de **tous** les comptes (regroupés par compte, avec recherche dans la liste), pour un message ou une sélection multiple ; glisser-déposer de messages sur un dossier d'un autre compte (Ctrl = copier)
  - [ ] **Dossier complet** : _Copier le dossier vers…_ / _Déplacer le dossier vers…_ un autre compte, avec option **sous-dossiers compris** ; recréation de l'arborescence sur le compte de destination (`camel_store_create_folder_sync`), puis transfert des messages dossier par dossier
  - [ ] Conservation des indicateurs (lu / non lu, suivi, catégories / mots-clés) et de la date de réception d'origine (`INTERNALDATE` IMAP) quand le serveur de destination le permet
  - [ ] Transfert par lots en arrière-plan, avec progression et annulation (§4) ; reprise possible après interruption (messages déjà copiés ignorés, repérés par `Message-ID`) ; rapport de fin (nombre de messages copiés, erreurs)
  - [ ] Déplacement : suppression à la source seulement après copie confirmée sur la destination ; dossiers spéciaux de la source (Boîte de réception, Envoyés…) jamais supprimés, seulement vidés
  - [ ] Confirmation avant un gros transfert (nombre de messages et taille estimés) ; avertissement si le quota du compte de destination risque d'être dépassé (§3)
- [ ] 🟡 **Nouveau dossier, Renommer, Copier, Déplacer, Supprimer** (`camel_store_create_folder_sync`, etc.), glisser-déposer dans l'arborescence — _fait : menu contextuel (nouveau, renommer, déplacer, copier, supprimer) ; reste : glisser-déposer_
- [x] 🟢 **Marquer tout comme lu**
- [x] 🟢 **Vider le dossier** : vider le dossier (vers la Corbeille), la Corbeille, les Indésirables
- [ ] 🟡 **Réorganiser l'ordre des dossiers** d'un compte dans l'arborescence (aujourd'hui triés par ordre alphabétique) : <a id="demo-dossiers"></a>
  - [x] 🟢 **Ordre par défaut « dossiers spéciaux d'abord »**, comme Outlook : Boîte de réception, Brouillons, Éléments envoyés, Éléments supprimés (Corbeille), Archive, Courrier indésirable, Boîte d'envoi, puis les autres dossiers par ordre alphabétique — dossiers spéciaux reconnus par leur rôle (`CAMEL_FOLDER_TYPE_*`, attributs IMAP SPECIAL-USE, dossiers distinctifs Exchange) et non par leur nom, quelle que soit la langue du serveur — _rôle du serveur, puis dossiers configurés dans le compte (Envoyés, Brouillons, Archive, Corbeille / Indésirables réels), en dernier recours nom usuel pour un rôle encore libre_
  - [x] 🟢 **Icône de la Boîte de réception** : remplacer le glyphe plein `\xE8A8` (enveloppe pleine, rendue comme un carré noir à cette taille) par une icône au trait cohérente avec les autres dossiers (ex. `\xE715`, enveloppe) — `FolderGlyph()` dans [MainWindow.xaml.cpp](winui/app/MainWindow.xaml.cpp) ; vérifier aussi `inbox_glyph`, qui sert à repérer le nœud Boîte de réception par son glyphe
  - [x] 🟢 Noms des dossiers spéciaux affichés dans la langue de l'interface (« INBOX » → « Boîte de réception », « Sent Items » → « Éléments envoyés »)
  - [ ] 🟡 **Ordre personnalisé** par glisser-déposer (ou _Monter_ / _Descendre_ dans le menu contextuel), mémorisé par compte, sans modifier les dossiers sur le serveur
  - [ ] 🟢 Choix du mode de tri par compte : dossiers spéciaux d'abord (défaut) / alphabétique (_Afficher tous les dossiers de A à Z_) / personnalisé ; _Rétablir l'ordre par défaut_
  - [ ] 🟢 Ordre des **comptes** eux-mêmes dans le volet des dossiers (compte par défaut en premier, puis glisser-déposer)
  - Compteurs de messages non lus et barre de défilement (décalage à gauche) : cf. §2
- [ ] 🟠 **Nettoyer le dossier** (et sous-dossiers) des messages redondants — même logique que §7
- [ ] **Récupérer les éléments supprimés** :
  - [ ] 🟡 IMAP : messages marqués supprimés non purgés
  - [ ] 🟠 **Exchange Online / Microsoft 365 : Récupération des éléments supprimés (« Recoverable Items » en self-service)** :
    - [ ] **Menu contextuel du compte** : option _« Récupérer les éléments supprimés du serveur… »_ ouverte par clic droit sur le compte Exchange (et sur le dossier _Éléments supprimés_ / _Corbeille_ de ce compte)
    - [ ] **Consultation du dossier des éléments récupérables** : fenêtre ou vue dédiée affichant la liste des messages présents dans le dossier de rétention serveur (`Recoverable Items` / `Deletions`, accessible via Microsoft Graph `/mailFolders/recoverableitemsdeletions/messages` ou EWS `recoverableitemsdeletions`) avec colonnes objet, expéditeur, date de suppression/réception, taille
    - [ ] **Restauration en self-service** : sélection unitaire ou multiple d'éléments avec bouton _Restaurer les éléments sélectionnés_ (rétablissement direct dans la Boîte de réception ou le dossier d'origine) et bouton _Supprimer définitivement_ (purge manuelle si autorisée par la stratégie de rétention)
    - [ ] **Recherche et tri** : filtrage par expéditeur, objet ou date de suppression pour localiser rapidement les courriels purgés par erreur
- [ ] 🟡 **Favoris** : section _Favoris_ en haut du volet des dossiers, réordonnable
- [x] 🟢 **Afficher dans le navigateur** (OWA / webmail du fournisseur)
- [ ] 🟠 **Dossiers de recherche** (dossiers virtuels `CamelVeeFolder`, aujourd'hui ignorés par `evo-core`), avec modèles prédéfinis : non lus, marqués, de personnes spécifiques, gros messages…
- [ ] **Propriétés du dossier** :
  - [ ] 🟡 Taille, nombre d'éléments, afficher le total ou les non-lus (quota du compte : §3)
  - [ ] 🟠 Autorisations du dossier (partage / délégation Exchange, ACL IMAP)
  - [ ] 🟠 Paramètres d'archivage automatique (âge, déplacement vers un dossier / fichier d'archive, suppression)
  - [ ] 🟠 Stratégie de rétention (Exchange)

## 10. Recherche — 🟡

La recherche dans le dossier courant existe déjà (filtrage instantané + recherche en-têtes et corps via `evo-core`).

### Recherche côté serveur (accélération)

- [ ] 🟠 **Utiliser l'API de recherche du serveur quand elle existe**, et ne faire la recherche locale « brute » qu'en repli :
  - [ ] 🟡 **Microsoft 365 / Exchange Online** : Microsoft Graph (`$search` sur `/me/messages` et `/me/mailFolders/{id}/messages`, ou API `/search/query` pour plusieurs dossiers / boîtes) ; événements et contacts par `$filter` / `$search` Graph
  - [ ] 🟡 **Exchange on-premise** : EWS `FindItem` avec `QueryString` (AQS, index de recherche du serveur) ou `Restriction` ; `ResolveNames` / `FindPeople` pour les contacts
  - [ ] 🟡 **Gmail / Google Workspace** : extension IMAP `X-GM-RAW` (syntaxe de recherche Gmail : `from:`, `has:attachment`, `after:`…) sur le compte IMAP existant, ou API Gmail `users.messages.list?q=` si la portée OAuth2 est accordée ; Calendar / People API pour événements et contacts
  - [ ] 🟢 **IMAP générique** : commande `SEARCH` du serveur (déjà utilisée par Camel IMAPX pour les messages non téléchargés), et extensions `ESEARCH` / `SEARCHRES` si annoncées
  - [ ] 🟢 **Repli local** (comptes locaux, serveur sans recherche, mode hors connexion) : recherche actuelle dans les résumés et le cache (`camel_folder_search_sync`), éventuellement avec l'index plein texte Camel (`camel-index-control`)
  - [ ] Choix automatique selon les capacités du compte (annonce `CAPABILITY` IMAP, type de collection EDS), sans réglage pour l'utilisateur
  - [ ] Traduction des critères de la recherche simple et avancée vers la syntaxe de chaque serveur (Graph `$search` / KQL, AQS EWS, `X-GM-RAW`, `SEARCH` IMAP) ; critères non pris en charge par le serveur appliqués ensuite en local sur les résultats
  - [ ] Résultats serveur portant sur des messages pas encore téléchargés : affichage des en-têtes reçus, téléchargement du message à l'ouverture
  - [ ] Résultats affichés progressivement (premiers résultats locaux immédiatement, complétés par ceux du serveur), annulables (§4)
  - [ ] Recherche multi-comptes (§11) : requêtes lancées en parallèle sur chaque compte, résultats fusionnés

### Recherche simple

- [x] 🟢 **Champ de recherche dans la barre de titre** de la fenêtre (au lieu de la vue Mail seule) <a id="demo-recherche-titre"></a> — _dossier courant en Courrier, contacts en Contacts ; barre de titre haute (48 px)_
- [x] 🟢 Bouton pour effacer la recherche ; raccourci Ctrl+E — _et Échap_
- [x] 🟢 **Portée** : dossier courant / sous-dossiers / compte courant / tous les comptes <a id="demo-recherche-portee"></a> — _sélecteur ComboBox dans la barre de titre, placeholder dynamique, parcours récursif Camel et étiquette d'origine_
- [ ] 🟢 **Recherche rapide de contacts** (champ dédié dans le ruban, comme Outlook) et accès au Carnet d'adresses
- [ ] 🟡 **Recherche dans le calendrier** (titre, lieu, description, participants)

### Recherche avancée

- [ ] 🟡 Accessible depuis le champ de recherche simple ; critères combinables : expéditeur, destinataire, objet, corps, plage de dates, taille, pièces jointes, lu / non lu, indicateur, catégorie
- [ ] 🟡 Opérateurs ET / OU entre les critères (expressions de recherche Camel, traduites vers la recherche serveur quand elle existe — cf. « Recherche côté serveur » ci-dessus)
- [ ] 🟠 Enregistrer une recherche comme dossier de recherche — dépend de §9

## 11. Vues unifiées multi-comptes — 🟠

- [ ] 🟠 **Mails** : Boîte de réception unifiée (et Envoyés, Brouillons, Corbeille), compte d'origine indiqué (colonne ou pastille de couleur) — dossiers virtuels Camel
- [ ] 🟡 **Calendriers** : événements de tous les calendriers de tous les comptes, une couleur par calendrier, chacun masquable — _fait dans la vue Calendrier : volet « Calendriers » par compte, cases à cocher mémorisées, couleur par calendrier_
- [ ] 🟡 **Contacts** : carnet d'adresses unifié, détection et regroupement des doublons (même email)
- [ ] 🟠 **Recherche dans les vues unifiées** (simple et avancée) pour mails, événements et contacts, compte d'origine visible dans les résultats
- [ ] 🟢 **Choix des comptes inclus** dans les vues unifiées (préférences)
- Tâches : affichage du compte d'origine et de l'adresse email associée dans la vue Tâches : cf. §16

## 12. Calendrier – Vues et navigation — 🔴

Les vues Grille (Jour, Semaine de travail, Semaine avec créneaux et gestion des chevauchements), Mois (6 semaines) et Liste sont déjà implémentées dans `MainWindow.Calendar.cpp`.

### Grille calendrier (brique préalable)

- [x] 🔴 **Contrôle grille Jour / Semaine de travail / Semaine** (WinUI n'en fournit pas) : créneaux horaires (24 h), événements positionnés et chevauchements en couloirs (_lanes_), bandeau journée entière en haut, défilement vertical centré sur 7 h, ligne rouge en temps réel de l'heure courante <a id="demo-grille-calendrier"></a> — _blocs colorés selon le calendrier, titre, horaires, lieu et tooltip, ouverture au clic/tap_
- [x] 🟠 **Vue Mois** : six semaines à partir du lundi précédent, jours hors mois estompés, 3 événements par jour puis « +N », clic sur un jour basculant en vue Jour
- [x] 🟢 Raccourcis Ctrl+Alt+1…5 entre les vues
- [ ] 🟢 **Échelle de temps** : 5, 6, 10, 15, 30, 60 minutes
- [x] 🟢 **Heures de travail** : jours ouvrés, heures de début / fin, premier jour de la semaine (options)
- [ ] 🟡 **Second fuseau horaire** dans la grille
- [ ] 🟠 **Glisser-déposer** d'un événement vers un autre créneau / jour / calendrier (déplacer, copier, redimensionner la durée) ; export par glisser-déposer vers l'extérieur : cf. §18

### Navigation

- [x] 🟢 **Barre de navigation** : _Aujourd'hui_, précédent / suivant, plage affichée (« 26 – 30 octobre 2026 ») <a id="demo-cal-navigation"></a> — _périodes Jour / Semaine / Mois, mémorisée_
- [x] 🟢 **Atteindre** : Aujourd'hui, 7 prochains jours, aller à une date (Ctrl+G)
- [ ] 🟡 **Mini-calendrier** dans le volet des dossiers (jours avec événements en gras)
- [ ] 🟡 Météo (optionnelle)

### Plusieurs calendriers

- [x] 🟢 **Couleur par calendrier** (couleur EDS `ESourceSelectable`), dans la liste et dans la grille <a id="demo-cal-couleurs"></a> — _liste des événements, volet des calendriers et blocs/barres colorés de la grille_
- [x] 🟠 **Superposé (vue globale)** : calendriers sélectionnés fusionnés dans une même grille — _déjà fonctionnel : événements des calendriers cochés regroupés et synchronisés dans la même vue_
- [ ] 🟠 **Côte à côte** : une colonne par calendrier, même période, défilement commun
- [ ] 🟢 Bascule superposé / côte à côte ; mémorisation du mode et des calendriers affichés — _fait : calendriers affichés mémorisés, calendrier actif des nouveaux événements_
- [ ] 🟡 **Groupes de calendriers** nommés (« Équipe », « Salles 2e étage ») pour en ouvrir plusieurs d'un coup

### Autres affichages

- [ ] 🟡 **Vues Liste / Actif** : liste filtrable des événements (à venir, périodiques…)
- [ ] 🟡 **Paramètres d'affichage** : champs, tri, filtre (mise en forme conditionnelle : cf. §17) ; 🟢 Rétablir l'affichage
- [ ] 🟡 **Volet de lecture** de l'événement sélectionné
- [ ] 🟠 **Liste des tâches quotidiennes** sous les vues Jour / Semaine (tâches EDS et messages en suivi par date d'échéance — dépend de §16)
- Espacement plus étroit, volet des dossiers réduit / désactivé : cf. §2

## 13. Calendrier – Rendez-vous, réunions et ressources — 🔴

- [ ] 🟠 **Éditeur d'événement complet** (brique préalable) : journée entière, périodicité (règles RRULE, exceptions), rappels, disponibilité (libre / occupé / provisoire / absent), catégories, privé, pièces jointes, description HTML <a id="demo-editeur-evenement"></a>
- [ ] 🟢 **Ajouter du temps de travail individuel** (bloc « focus » marqué occupé, sans participants)
- [ ] 🔴 **Nouvelle réunion** : participants (obligatoires / facultatifs), envoi des invitations, suivi des réponses (accepté / provisoire / refusé), mise à jour et annulation (iTIP / iMIP ; géré par le serveur pour Exchange)
- [ ] 🟡 **Répondre aux invitations reçues** depuis le volet de lecture du Mail (Accepter / Provisoire / Refuser / Proposer un nouvel horaire) <a id="demo-invitations"></a>
- [ ] 🔴 **Affichage Planification** (Scheduling Assistant) : une ligne par personne / ressource, heures en abscisse, créneaux libres / occupés / provisoires / absent (EWS `GetUserAvailability`, Graph `getSchedule`, CalDAV free-busy), suggestion de créneau commun
- [ ] **Calendriers de ressources** (salles, équipements) :
  - [ ] 🟠 Parcourir / rechercher les listes de salles Exchange (_room lists_)
  - [ ] 🟡 Disponibilité en vue Planification
  - [ ] 🟡 Réserver une ressource à la création d'une réunion (participant ressource, acceptation / refus automatique)
- [ ] 🟠 **Réunion en ligne** : bouton « Réunion instantanée » / lien de visioconférence, générique et extensible par fournisseur (Teams via Graph en priorité pour M365)
- [ ] 🟡 **Glisser-déposer d'un email vers le calendrier** pour créer un rendez-vous
- [ ] 🟠 **Emplacement de travail** par jour / créneau (bureau, télétravail, hors ligne), synchronisé avec Exchange Online si disponible, visible en vue Planification

## 14. Calendrier – Calendriers partagés et externes — 🟠

- [ ] **Ajouter un calendrier** :
  - [ ] 🟢 Nouveau calendrier vide (local ou sur le serveur du compte)
  - [ ] 🟢 À partir d'Internet : URL ICS / webcal (backend EDS `webcal` existant)
  - [ ] 🟡 CalDAV par URL
  - [ ] 🟡 À partir d'un fichier `.ics`
  - [ ] 🟠 À partir du carnet d'adresses / de l'annuaire : calendrier d'un autre utilisateur Exchange / M365 selon les droits accordés (disponibilités, détails limités, lecture complète, délégué)
  - [ ] 🟠 À partir d'une liste de salles
- [ ] 🟡 Plusieurs calendriers partagés ouverts simultanément et conservés (groupe « Calendriers partagés »)
- [ ] **Partager le calendrier** :
  - [ ] 🟠 Invitation de partage avec niveaux de droits (disponibilités, titres et lieux, tous les détails, édition, délégué) — Exchange / M365
  - [ ] 🟡 Envoyer un extrait par email (plage de dates, niveau de détail)
  - [ ] 🔴 Publication en ligne (URL ICS publique)

### Hiérarchie complète des groupes de calendriers Exchange Online / Microsoft 365 — 🟠

Constat : sur un compte Exchange Online / Microsoft 365 (ex. EVAM), EvolutionWin n'affiche aujourd'hui qu'une liste plate et très partielle de calendriers découverts (quelques collègues seulement sous le compte), alors qu'Outlook reflète l'intégralité des calendriers organisés en groupes structurés (_Mes calendriers_, _Équipe : Manager_, _Équipe : Mes collaborateurs direct reports_, _Autres calendriers_, _Salles_).

- [ ] 🟠 **Découverte exhaustive et reflet fidèle de tous les calendriers Exchange Online / M365 (Graph / EWS)** :
  - [ ] 🟠 **Exploration complète de l'API Graph / EWS** : interroger tous les groupes de calendriers (`GET /me/calendarGroups` et `GET /me/calendarGroups/{id}/calendars` sous Microsoft Graph, ou `FindFolder` / `CalendarFolder` avec `calendarGroup` sous EWS) pour récupérer l'ensemble des calendriers personnels, souscrits, partagés, d'équipe et de ressources (salles de réunion), sans omission.
  - [ ] 🟡 **Arborescence hiérarchique par groupes dans le volet latéral** : reproduire fidèlement l'organisation d'Outlook avec groupes repliables/dépliables (_Mes calendriers_, _Équipe : Nom du manager_, _Équipe : Mes collaborateurs_, _Autres calendriers_, _Salles_), au lieu d'une simple liste plate.
  - [ ] 🟢 **Sélection et couleurs individuelles** : case à cocher par calendrier, couleur d'accent propre conservée, mémorisation des calendriers cochés entre les sessions.
  - [ ] 🟡 **Gestion des groupes de calendriers** : création, renommage et suppression de groupes de calendriers répercutés sur le serveur Exchange Online.

- Calendriers de groupes Microsoft 365 : cf. « Groupes Microsoft 365 », §7

## 15. Contacts — 🟡

- [ ] 🟡 **Carnet d'adresses global** (GAL) Exchange / M365 : recherche et sélection des destinataires (backend evolution-ews) <a id="demo-gal"></a>
- Contacts unifiés et doublons : cf. §11 ; recherche rapide de contacts : cf. §10 ; export par glisser-déposer vers l'extérieur : cf. §18
- [ ] 🟡 **Fenêtre de sélection de destinataires** (carnet d'adresses, comme le bouton _À…_ d'Outlook) :
  - [ ] Ouverte par un bouton en face de chaque ligne _À_, _Cc_, _Cci_ de la fenêtre de rédaction, et en face de la ligne des participants (_Obligatoires_, _Facultatifs_, _Ressources_) d'une invitation (§13)
  - [ ] Fenêtre indépendante redimensionnable : choix du carnet d'adresses (tous les carnets, carnet d'un compte, annuaire GAL Exchange / M365, listes de salles), champ de recherche (nom, prénom, adresse, société), liste des résultats (nom, adresse, société, téléphone)
  - [ ] Boutons _À →_, _Cc →_, _Cci →_ (ou _Obligatoires →_, _Facultatifs →_, _Ressources →_ pour une invitation) et double-clic pour ajouter ; sélection multiple ; destinataires choisis affichés en bas de chaque ligne, supprimables
  - [ ] Listes de distribution / groupes de contacts : ajout du groupe entier ou développement en membres
  - [ ] _OK_ : les destinataires sont ajoutés aux champs correspondants de la rédaction / de l'invitation ; _Annuler_ : aucun changement
  - [ ] Fiche du contact accessible depuis la liste (clic droit › _Propriétés_), création d'un nouveau contact depuis la fenêtre
- [ ] 🟡 **Autocomplétion des adresses** dans les champs _À_, _Cc_, _Cci_ de la rédaction et dans les participants d'une invitation : <a id="demo-autocompletion"></a>
  - [x] Suggestions au fil de la frappe (à partir de 2 caractères) depuis tous les carnets d'adresses (requête préfixe `EBookClient` sur nom, prénom, adresse), puis l'annuaire GAL pour les comptes Exchange / M365
  - [x] Adresses récemment utilisées en tête (destinataires des derniers messages envoyés), même si absentes des contacts
  - [x] Liste déroulante : nom, adresse, carnet d'origine ; navigation aux flèches, validation par Entrée / Tab / point-virgule
  - [ ] Destinataire validé affiché en « jeton » (nom, adresse en info-bulle), supprimable, double-clic pour le modifier ; adresse inconnue ou mal formée soulignée
  - [ ] Plusieurs adresses collées d'un coup (séparées par `;` ou `,`) découpées en jetons
  - [x] Suppression d'une suggestion récente (bouton ✕ dans la liste et touche Suppr au clavier)
- [ ] 🟡 **Créer rapidement une fiche contact depuis un mail ou une invitation** :
  - [ ] Clic droit (ou survol) sur une adresse — expéditeur, destinataires, Cc, organisateur ou participant d'une invitation — : _Ajouter aux contacts_, _Modifier le contact_ si l'adresse est déjà connue
  - [ ] Fiche préremplie : nom et adresse email tirés de l'en-tête ; 🟠 en option, téléphone, société, fonction et site web extraits de la signature du message
  - [ ] 🟡 Pour les comptes Exchange / M365 : compléter la fiche depuis l'annuaire (GAL) si la personne y figure
  - [ ] Choix du carnet d'adresses de destination (carnet par défaut, §3) ; détection des doublons (adresse déjà présente → proposer de compléter la fiche existante)
  - [ ] Ajout en un clic sans ouvrir la fiche (enregistrement direct) ou ouverture de la fiche pour compléter
  - [ ] Ajout groupé : tous les participants d'une invitation / tous les destinataires d'un mail
  - [ ] Indicateur visuel sur les adresses déjà présentes dans les contacts (carte de contact au survol : nom, société, téléphone, actions _Écrire_, _Planifier une réunion_)

## 16. Tâches — 🟠

Les listes de tâches EDS (`E_SOURCE_EXTENSION_TASK_LIST`, composants `VTODO`) ne sont pas encore exploitées par `evo-core` ; evolution-ews les synchronise déjà pour Exchange (et Microsoft To Do via Graph à prévoir).

### Module Tâches

- [x] 🟡 **Accès aux tâches dans `evo-core`** : lister, créer, modifier, supprimer (`E_CAL_CLIENT_SOURCE_TYPE_TASKS`)
- [x] 🟡 **Vue Tâches** (entrée _Tâches_ de la barre latérale) : liste des tâches par liste / compte, avec colonnes objet, échéance, priorité, état, catégorie
- [x] 🟡 **Affichage du compte source dans la vue Tâches** : afficher pour chaque tâche le compte (nom du compte et adresse email associée à la tâche pour ce compte) duquel est issue la tâche affichée (badge dédié avec icône et libellé du compte et de son adresse email)
- [x] 🟡 **Éditeur de tâche** : objet, description, date de début, échéance, priorité, état (non commencée, en cours, terminée, en attente, différée), pourcentage d'achèvement, catégories
- [ ] 🟠 Tâches périodiques
- [ ] 🟠 Rappel sur une tâche — dépend de la fenêtre des rappels (§2)
- [x] 🟢 **Marquer comme terminée** (case à cocher dans la liste), masquer / afficher les tâches terminées
- [x] 🟢 **Filtres et tri** : aujourd'hui, en retard, cette semaine, sans date, par priorité / échéance
- [x] 🟢 **Nouvelle tâche** depuis le menu _Nouveaux éléments_ et raccourci (Ctrl+Maj+K)
- [ ] 🟡 Plusieurs listes de tâches (créer, renommer, supprimer)
- [ ] 🟠 Tâches Microsoft To Do (Graph) pour les comptes M365

### Lien entre mails, événements et tâches

- [x] 🟡 **Action « Créer une tâche »** directement depuis un mail ou un événement du calendrier : <a id="demo-creer-tache"></a>
  - [x] Accès : bouton du ruban (Ctrl+Maj+T) et de la fenêtre de lecture, menu contextuel du message, bouton dans le dialogue d'événement
  - [x] Depuis un mail : objet → titre, expéditeur / date / corps → description, indicateur de suivi → priorité haute, échéance par défaut à J+1
  - [x] Depuis un événement : titre, description et lieu repris, échéance proposée = date de début de l'événement
  - [ ] Création rapide (saisie de l'échéance et de la priorité dans un petit menu, sans ouvrir l'éditeur) ou ouverture de l'éditeur complet
  - [ ] Indicateur « tâche créée » sur le mail / l'événement d'origine
- [ ] 🟠 **Attribuer un mail à une tâche existante** : sélection de la tâche dans une liste, plusieurs mails possibles par tâche
  - [ ] Lien conservé par l'en-tête `Message-ID` (propriété `X-` / `ATTACH` dans le `VTODO`), ouverture du mail d'origine depuis la tâche
  - [ ] Indicateur sur le mail (icône « lié à une tâche ») et accès à la tâche depuis le volet de lecture
- Liste des tâches sous les vues Jour / Semaine du calendrier : cf. §12

### Barre latérale des tâches (à droite)

- [x] 🟡 **Volet latéral droit** affichant une liste courte des tâches ouvertes, disponible dans les vues Mail, Calendrier et Contacts (équivalent de la To-Do bar d'Outlook) <a id="demo-volet-taches"></a>
  - [x] Ordre : tâches en retard en haut, puis par **échéance la plus proche** ; à échéance égale, par **priorité décroissante** (haute → basse) ; tâches sans échéance en bas
  - [x] Ligne compacte : case _Terminé_, titre, échéance relative (« Aujourd'hui », « Demain », « En retard » en rouge), priorité, compte source
  - [x] Champ « Nouvelle tâche » en haut (saisie rapide + touche Entrée)
  - [x] Clic : ouvre l'éditeur complet de tâche
  - [x] Volet affichable / masquable (bouton _Volet des tâches_ du ruban, raccourci clavier universel `Alt+F2`), largeur redimensionnable par séparateur `PaneSplitter` et mémorisée dans `settings.json`, lien _Voir tout (Ctrl+4)_
  - [x] Mise à jour en direct lors des créations, modifications, suppressions et synchronisations du serveur
- [x] 🟢 **Recherche dans les tâches** — cf. §10 : filtrage instantané par mot-clé (objet, description, catégories, compte source), recherche intégrée dans la barre de titre (Ctrl+E) et champ de recherche dédié dans la vue Tâches

## 17. Catégories et codes couleur — 🟡

- [ ] 🟡 **Catégories communes** à tous les types d'éléments : nom + couleur (palette d'une vingtaine de couleurs), gérées dans les Réglages (créer, renommer, changer la couleur, supprimer) <a id="demo-categories"></a>
  - [ ] Stockage : étiquettes Camel pour les mails, propriété `CATEGORIES` pour les contacts (vCard), les événements et les tâches (iCalendar)
  - [ ] 🟠 Synchronisation avec la liste de catégories principale Exchange / M365 (couleurs comprises)
- [ ] 🟡 **Catégoriser** un élément (plusieurs catégories possibles) : menu contextuel, bouton _Catégoriser_ du ruban, catégories récentes ; raccourci vers une catégorie (Ctrl+F2…F12)
- [ ] **Affichage des codes couleur** :
  - [ ] 🟡 **Mails** : pastilles de couleur dans la liste des messages et dans l'en-tête du volet de lecture
  - [ ] 🟡 **Contacts** : pastilles dans la liste et sur la fiche
  - [ ] 🟡 **Événements** : couleur de la catégorie dans la grille du calendrier (à la place ou en complément de la couleur du calendrier, au choix)
  - [ ] 🟡 **Tâches** : pastilles dans la liste des tâches
- [ ] 🟠 **Mise en forme conditionnelle** : couleur automatique selon des règles (expéditeur, domaine, mots de l'objet, importance, organisateur…), pour la liste des messages (§5) comme pour le calendrier (§12)
- [ ] 🟢 **Filtrer / trier / regrouper par catégorie** dans toutes les vues ; recherche par catégorie (§10)

## 18. Import / Export — 🟡

### Export par glisser-déposer vers l'extérieur (Drag and drop)

- [ ] 🟡 **Export d'un mail, d'un événement ou d'un contact par simple glisser-déposer vers l'extérieur (Explorateur Windows, Bureau)** :
  - Déposer un ou plusieurs éléments depuis l'interface d'EvolutionWin vers l'Explorateur de fichiers Windows, le Bureau ou une autre application pour les exporter directement sous forme de fichiers natifs. Réutilise les fonctions d'export existantes en générant les fichiers dans un dossier temporaire avant de les transférer via le `DataPackage` (`SetStorageItems`).
  - [ ] 🟡 **Courriels (format `.eml`)** : glisser un ou plusieurs messages sélectionnés depuis la liste des messages vers l'extérieur ; réutilisation de la fonction d'export EML existante (`evo_core_export_messages_eml` / `ExportMessagesEml`)
  - [ ] 🟡 **Événements de calendrier (format `.ics`)** : glisser un événement depuis la vue calendrier vers l'extérieur pour générer un fichier iCalendar (`.ics`) ; réutilisation de l'export iCalendar existant
  - [ ] 🟡 **Contacts (format `.vcf`)** : glisser un contact depuis la vue des contacts vers l'extérieur pour générer un fichier vCard (`.vcf`) ; réutilisation de l'export vCard existant
  - [ ] 🟢 **Sélection multiple** : prise en charge du glisser-déposer de plusieurs éléments sélectionnés simultanément (génération par lot)
  - [ ] 🟢 **Gestion des fichiers temporaires** : nettoyage automatique des fichiers temporaires créés après confirmation du dépôt ou annulation

### Assistant et formats d'export

- [ ] 🟡 **Assistant d'export** (_Fichier › Exporter_) : compte, éléments (emails, contacts, calendriers, tâches), dossier de destination
- [ ] **Emails** :
  - [ ] 🟡 Format mbox (un fichier par dossier, arborescence conservée)
  - [ ] 🟢 Format EML (un fichier `.eml` par message, arborescence conservée ; export `.eml` existant dans `evo-core`)
  - [ ] 🟢 Choix des dossiers (tous / sélection / dossier courant)
- [x] 🟢 **Contacts** : vCard (`.vcf`, fichier unique ou un par contact) ; 🟡 CSV en option
- [x] 🟢 **Calendriers et tâches** : iCalendar (`.ics`, un fichier par calendrier)
- [ ] 🟡 **Options** : filtre par plage de dates, export en arrière-plan avec progression (dépend de §4), rapport de fin
- [ ] 🟠 **Import** : mbox / EML, vCard, iCalendar (_Fichier › Ouvrir et exporter_)

## 19. Configuration de l'application — 🟡

Page _Paramètres_ regroupant les préférences générales (les réglages propres à un compte restent au §3). Préférences enregistrées dans un fichier de configuration utilisateur (`%LOCALAPPDATA%\EvolutionWin`), l'application n'étant pas empaquetée.

### Réorganisation de la page Paramètres

Aujourd'hui, une seule page à faire défiler (`SettingsViewPane`) enchaîne Comptes, Confidentialité, Signatures et Signature par compte, sans navigation ni regroupement logique ; elle deviendra illisible avec les réglages à venir.

- [x] 🟡 **Navigation par catégories** sur le modèle des Paramètres de Windows 11 : liste des catégories à gauche (`NavigationView` en mode gauche ou liste), contenu de la catégorie à droite, fil d'Ariane en haut (« Paramètres › Comptes › Compte X ») <a id="demo-parametres"></a>
  - [x] Panneau dédié regroupant les différents paramètres : conteneur structuré à droite organisant les options de la catégorie active sous forme de sections thématiques et de cartes de réglages (`SettingsGroup` / `SettingsCard`), avec titre, description, contrôle interactif et résumé d'état
  - [x] Catégories proposées : **Comptes**, **Courrier** (lecture, rédaction, signatures, contenu externe), **Calendrier**, **Contacts**, **Tâches**, **Notifications**, **Apparence** (thème, langue, disposition), **Confidentialité et sécurité**, **Mise à jour**, **Avancé** (caches, journaux, réinitialisation), **À propos**
- Page détaillée par compte (clic sur un compte dans la catégorie Comptes) : cf. « Modifier un compte existant », §3
- [x] 🟢 **Contrôles homogènes** : cartes de réglage façon Windows 11 (`SettingsCard` / `SettingsExpander` du Windows Community Toolkit, ou équivalent maison) — titre, description courte, contrôle aligné à droite ; réglages avancés repliés dans un expandeur
- [x] 🟢 **Recherche dans les Paramètres** : champ en haut de la page qui filtre les réglages par mots-clés et ouvre directement la bonne catégorie
- [x] 🟢 **Enregistrement immédiat** des modifications (pas de bouton _Enregistrer_ global) avec confirmation discrète ; boutons _Annuler_ / _Enregistrer_ seulement dans les éditeurs (signature, compte)
- [x] 🟢 **Accès contextuel** : liens directs depuis l'interface vers le réglage concerné (ex. bandeau « contenu externe bloqué » → _Confidentialité_, icône d'état des connexions → page du compte, menu contextuel d'un compte → _Paramètres du compte_)
- [x] 🟢 **Rétablir les valeurs par défaut** par catégorie
- [x] 🟢 Ouverture des Paramètres par Ctrl+, et depuis _Fichier › Options_
- [x] 🟢 Textes d'aide courts sous chaque réglage, sans jargon technique (laisser les détails de protocole dans la page _Avancé_)

### Notifications

- [ ] 🟢 **Activer / désactiver les notifications Windows** (toasts) de nouveau courrier, globalement et par compte / dossier
- [x] 🟢 **Notifications discrètes : Toasts système et liste déroulante dans la barre d'état** :
  - [x] **Suppression du bandeau intrusif en bas de fenêtre** : suppression de l'InfoBar temporaire en bas de l'application pour les nouveaux messages reçus afin de préserver l'espace de travail.
  - [x] **Émission exclusive via les Toasts système Windows** : notification par le Centre de notifications Windows (`Microsoft.Windows.AppNotifications`), respectant le mode confidentiel et le paramétrage du système.
  - [x] **Bouton et menu déroulant (Flyout) dans la barre d'état** : placé dans la barre d'état permanente en bas, immédiatement à côté du statut de connexion (« Connecté ») :
    - [x] Icône cloche avec badge / compteur indiquant le nombre de notifications actives (masqué si aucune notification).
    - [x] Menu déroulant (`Flyout`) affichant l'historique chronologique des notifications reçues (expéditeur, objet ou extrait, horodatage).
    - [x] Bouton de fermeture individuel (`✕`) sur chaque notification permettant de la faire disparaître de la liste et de décrémenter le compteur.
    - [x] Bouton _« Tout effacer »_ / _« Tout marquer comme lu »_ pour vider l'ensemble des notifications d'un clic.
    - [ ] Clic sur une notification pour naviguer directement vers le message ou dossier concerné.
- [x] 🟢 **Mode confidentiel des notifications** : remplacer l'expéditeur, l'objet et l'extrait par un message générique (« Nouveau message », « 3 nouveaux messages ») — utile en présentation ou en partage d'écran <a id="demo-confidentiel"></a>
  - [x] Bascule rapide depuis la barre d'état / le menu de l'icône, sans passer par les Paramètres — _ruban Affichage, barre d'accès rapide et Paramètres › Confidentialité_
  - [ ] 🟡 Activation automatique quand Windows est en mode _Ne pas déranger_ / partage d'écran détecté (optionnel)
  - [ ] S'applique aussi aux rappels de rendez-vous et de tâches (titre et lieu masqués) — _quand les rappels existeront (§2)_
- [ ] 🟢 Son de notification (aucun / son Windows / personnalisé), badge de non-lus sur l'icône de la barre des tâches
- [ ] 🟢 Notifications des rappels de calendrier et de tâches (activer / désactiver, durée d'affichage)

### Général

- [ ] 🟢 Langue de l'interface (cf. §20), thème (clair / sombre / système), taille du texte
- [ ] 🟢 Démarrage avec Windows, réduction dans la zone de notification à la fermeture
- [x] 🟢 Module affiché au démarrage (Mail, Calendrier, Contacts, Tâches)
- [ ] 🟡 Raccourcis clavier (afficher la liste, profil Outlook / Evolution)

### Mail

- [ ] 🟢 Intervalle de relève automatique (5 minutes actuellement ; valeur par défaut, remplaçable par compte, §3), relève au démarrage
- [x] 🟢 Marquer comme lu : immédiatement / après N secondes / au changement de sélection / jamais à l'affichage dans le volet de lecture
- [ ] 🟢 Format de rédaction par défaut (HTML / texte brut), police et taille par défaut, correcteur orthographique (langue)
- [x] 🟢 Confirmations : avant suppression définitive, avant d'envoyer sans objet, pièce jointe mentionnée mais absente
- [x] 🟢 Vider la Corbeille à la fermeture
- [ ] 🟢 Contenu externe : cf. §6 ; signatures : cf. §3

### Calendrier

- [x] 🟢 Heures et jours de travail, premier jour de la semaine, fuseau horaire, numéros de semaine — cf. §12
- [x] 🟢 Rappel par défaut des nouveaux événements, durée par défaut des réunions

### Confidentialité et sécurité

- [ ] 🟢 Masquer l'aperçu du message dans la liste (lignes d'extrait, §5) en même temps que le mode confidentiel des notifications
- [ ] 🟡 Verrouiller l'application après inactivité (Windows Hello)
- [ ] 🟢 Effacer les caches (WebView2, aperçus de pièces jointes)

### Sauvegarde des préférences

- [ ] 🟡 Exporter / importer la configuration (préférences, signatures, règles, catégories) pour la reprendre sur un autre poste

## 20. Traductions de l'interface — 🟠

Les textes d'EvolutionWin sont aujourd'hui écrits en dur en français dans le code et le XAML. Les traductions d'EDS, evolution-ews, GLib, etc. (`.mo`) sont déjà installées par le MSI.

### Internationalisation de l'application

- [ ] 🟠 **Extraire tous les textes** du XAML et du C++ dans des fichiers de ressources (`Resources.resw` par langue, `x:Uid` dans le XAML, `ResourceLoader` / MRT Core dans le code) — prérequis à toute traduction
- [ ] 🟡 Textes construits dynamiquement : pluriels (« 1 message » / « 3 messages »), paramètres insérés dans les phrases (« Compte {0} : {1} non lus »)
- [ ] 🟢 Dates, heures, nombres et tailles formatés selon la langue (`Windows.Globalization`)
- [ ] 🟡 Messages d'erreur d'`evo-core` traduits (chaînes C passées par gettext avec un domaine `evo-core`, ou codes d'erreur traduits côté interface)
- [ ] 🟢 Préfixes de réponse / transfert selon la langue (« RE: » / « TR: », « AW: » / « WG: », « R: » / « I: »…) et en-tête de citation traduit

### Langues

- [ ] 🟢 Français (langue source actuelle)
- [ ] 🟡 Anglais
- [ ] 🟡 Allemand
- [ ] 🟡 Italien
- [ ] 🟡 Espagnol, portugais, néerlandais (autres langues selon les besoins ; s'appuyer sur les traductions existantes d'Evolution pour la terminologie)
- [ ] 🟢 Processus de traduction : fichiers `.resw` (ou `.po` convertis) relus par des locuteurs natifs, vérification des clés manquantes à la compilation

### Choix de la langue

- [ ] 🟢 **Par défaut** : langue d'affichage de Windows, repli sur l'anglais si elle n'est pas traduite
- [ ] 🟢 **Dans les Paramètres** (§19) : choix manuel de la langue, appliqué au redémarrage (`ApplicationLanguages.PrimaryLanguageOverride`)
- [ ] 🟡 **Dans le MSI** : choix de la langue par défaut à l'installation
  - [ ] Liste déroulante dans l'interface d'installation (boîte de dialogue WiX `WixUI` personnalisée), préremplie avec la langue de Windows
  - [ ] Propriété publique `UILANGUAGE` pour le déploiement silencieux (`msiexec /i Evolution-<version>-x64.msi /qn UILANGUAGE=de-DE`, GPO / Intune)
  - [ ] Valeur écrite dans le registre machine (`HKLM\Software\Evolution-WindowsPort\UILanguage`), utilisée tant que l'utilisateur n'a pas choisi une autre langue dans les Paramètres
  - [ ] Option : installer toutes les langues ou seulement la langue choisie (fonctionnalités MSI par langue)
- [ ] 🟡 **Interface du MSI elle-même traduite** (MSI multilingue par transformations de langue, ou un MSI par langue)
