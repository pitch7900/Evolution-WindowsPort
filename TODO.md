<!-- markdownlint-disable MD033 -- ancres <a id> utilisées par les liens des priorités et des doublons -->
# TODO

## Priorités pour une démo fonctionnelle

Hypothèse : démo de type « remplaçant d'Outlook » avec un compte Microsoft 365 et un compte IMAP. Critères : ce qui se voit en démo et ce qui risque de la faire échouer, au meilleur rapport effet / coût.

### Priorité 1 — corrections rapides et très visibles (≈ 1 à 2 semaines)

| # | Tâche | § | Difficulté |
| --- | --- | --- | --- |
| 1 | [Icône de la Boîte de réception, dossiers spéciaux en premier, noms traduits](#demo-dossiers) | 9 | 🟢 |
| 2 | [Mode confidentiel des notifications](#demo-confidentiel) (utile pendant la démo elle-même) | 19 | 🟢 |
| 3 | [Position du volet de lecture](#demo-volet-lecture) (droite / bas) | 2 | 🟢 |
| 4 | [Nouveaux éléments](#demo-nouveaux-elements), [Archiver, Copier](#demo-archiver) | 7 | 🟢 |
| 5 | [Champ de recherche dans la barre de titre, Ctrl+E](#demo-recherche-titre) | 10 | 🟢 |
| 6 | Calendrier : [barre de navigation](#demo-cal-navigation), [couleur par calendrier](#demo-cal-couleurs) | 12 | 🟢 |

### Priorité 2 — les gestes de base d'un utilisateur Outlook (≈ 3 à 5 semaines)

| # | Tâche | § | Difficulté |
| --- | --- | --- | --- |
| 7 | [Connexion Gmail compatible avec la double authentification](#demo-gmail-2fa) (bogue : « identifiants invalides ») — prérequis de tout compte Gmail | 3 | 🟡 |
| 8 | Calendrier et contacts du compte en ligne : [Microsoft 365 / Exchange Online](#demo-cal-contacts-m365) (🟡), [Gmail / Google Workspace](#demo-cal-contacts-gmail) (🟠) | 3 | 🟡 / 🟠 |
| 9 | [Gestion simple du calendrier](#demo-gestion-calendrier) (créer, **modifier**, supprimer un événement) et [des contacts](#demo-gestion-contacts) (créer, **modifier**, supprimer) | 3 | 🟡 |
| 10 | [Autocomplétion des destinataires](#demo-autocompletion) | 15 | 🟡 |
| 11 | [Simplifier l'assistant d'ajout de compte](#demo-assistant-compte) | 3 | 🟡 |
| 12 | [Modifier un compte existant](#demo-modifier-compte) | 3 | 🟡 |
| 13 | [Indicateur d'état des connexions / hors connexion et *Se reconnecter*](#demo-etat-connexions) | 4 | 🟡 |
| 14 | [Barre de progression du rafraîchissement](#demo-progression) | 4 | 🟡 |
| 15 | [Double-clic : message dans une fenêtre indépendante](#demo-fenetre-message) | 6 | 🟡 |
| 16 | [Répondre aux invitations reçues](#demo-invitations) | 13 | 🟡 |
| 17 | [Volet des tâches à droite](#demo-volet-taches) et [« Créer une tâche » depuis un mail](#demo-creer-tache) | 16 | 🟡 |
| 18 | [Catégories communes en couleur](#demo-categories) | 17 | 🟡 |
| 19 | [Recherche sur tous les comptes](#demo-recherche-portee) | 10 | 🟡 |
| 20 | [Réorganisation de la page Paramètres](#demo-parametres) | 19 | 🟡 |

### Priorité 3 — ce qui fait vraiment « Outlook » (gros chantiers)

| # | Tâche | § | Difficulté |
| --- | --- | --- | --- |
| 21 | [Grille calendrier Jour / Semaine](#demo-grille-calendrier) — plus gros écart visible avec Outlook | 12 | 🔴 |
| 22 | [Éditeur d'événement complet](#demo-editeur-evenement) (périodicité, rappels) | 13 | 🟠 |
| 23 | [Grouper par discussion](#demo-conversations) | 5 | 🟠 |
| 24 | [Réponses automatiques](#demo-reponses-auto) Microsoft 365 / Exchange (si démo sur M365) | 3 | 🟡 |
| 25 | [Carnet d'adresses global (GAL)](#demo-gal) (si démo sur M365) | 15 | 🟡 |

**Après la démo** : aperçu des pièces jointes, règles, mode hors connexion complet, vues unifiées, traductions, import / export, mise à jour automatique, installation silencieuse avancée, nettoyage à la désinstallation, boîte Prioritaire, réunions avec invitations et Planification (🔴).

### Conventions

- Une tâche n'est cochable (`- [ ]`) qu'à **un seul endroit** ; ailleurs, un simple renvoi sans case à cocher (« cf. §n »).
- Drapeaux des messages : le **suivi** (libellé Urgent / À traiter… + échéance) et l'**importance** sont distincts (§7) ; les couleurs relèvent des **catégories** (§17).

## Légende des difficultés

| Niveau | Signification |
| --- | --- |
| 🟢 Facile | Interface seule ou appel direct d'une fonction EDS / Camel existante — de quelques heures à 1 jour |
| 🟡 Moyenne | Nouvelle fonction dans `evo-core` + interface, sans difficulté technique particulière — quelques jours |
| 🟠 Difficile | Nouveau composant, protocole spécifique (EWS, Graph, Sieve…) ou logique non triviale — 1 à 2 semaines |
| 🔴 Très difficile | Nouvelle brique majeure (contrôle personnalisé complexe, moteur de règles, flux d'invitations…) — plusieurs semaines |

La difficulté tient compte de l'existant (cf. [WINDOWS-PORT.md](WINDOWS-PORT.md), phase 6b) : recherche dans le dossier courant, blocage du contenu distant dans WebView2, export `.eml`, sélection multiple, déplacement, drapeau, impression, vue calendrier mensuelle simple et liste de contacts sont déjà réalisés.

## Vue d'ensemble

| # | Fonctionnalité | Difficulté globale |
| --- | --- | --- |
| 1 | Application et installation | 🟢 |
| 2 | Interface générale et disposition | 🟡 |
| 3 | Comptes | 🟠 |
| 4 | Synchronisation et mode hors connexion | 🟠 |
| 5 | Mail – Liste des messages | 🟠 |
| 6 | Mail – Lecture des messages | 🟢 |
| 7 | Mail – Actions sur les messages | 🟠 |
| 8 | Mail – Règles et courrier indésirable | 🔴 |
| 9 | Mail – Gestion des dossiers | 🟠 |
| 10 | Recherche | 🟡 |
| 11 | Vues unifiées multi-comptes | 🟠 |
| 12 | Calendrier – Vues et navigation | 🔴 |
| 13 | Calendrier – Rendez-vous, réunions et ressources | 🔴 |
| 14 | Calendrier – Calendriers partagés et externes | 🟠 |
| 15 | Contacts | 🟡 |
| 16 | Tâches | 🟠 |
| 17 | Catégories et codes couleur | 🟡 |
| 18 | Import / Export | 🟡 |
| 19 | Configuration de l'application | 🟡 |
| 20 | Traductions de l'interface | 🟠 |

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
  - [x] *Paramètres › Applications installées* (propriété `ARPPRODUCTICON` du MSI)
- [x] 🟢 **Mise à jour du MSI : erreur 1618** « Une autre installation est en cours d'exécution » à l'installation de la 0.9.1 par-dessus la 0.9.0 — cause : dialogue « fichiers en cours d'utilisation » caché (EvolutionWin et services EDS ouverts), une seconde installation lancée entre-temps échouait ; le MSI ferme désormais ces programmes (`util:CloseApplication`)
- [ ] 🟡 **Option « installation silencieuse » du MSI avec fermeture forcée de l'application** — *existant : `util:CloseApplication` ferme déjà EvolutionWin et les services EDS / `gdbus` à chaque installation*
  - [ ] Propriété publique documentée (ex. `FORCECLOSE=1`), utilisable avec `/qn` ou `/passive` : `msiexec /i Evolution-<version>-x64.msi /qn FORCECLOSE=1` (déploiement GPO / Intune, mise à jour automatique) — *aujourd'hui `/qn` fonctionne déjà sans propriété : la fermeture est toujours forcée (vérifié sur la VM de test)*
  - [ ] Fermeture de **toutes les instances**, y compris dans les **autres sessions utilisateur** (serveur multi-sessions, session déconnectée) : demande de fermeture propre, puis arrêt forcé après un délai court — *fait pour la session courante (message de fermeture puis arrêt après 5 s) ; autres sessions non testées*
  - [ ] Fermeture de **toutes les dépendances** lancées par EvolutionWin (*fait : services EDS et `gdbus` ; reste : `camel-index-control`, `gspawn-win64-helper`, `msedgewebview2.exe`*) : services EDS (`evolution-source-registry`, fabriques carnet d'adresses / calendrier et leurs sous-processus), `gdbus`, `camel-index-control`, `gspawn-win64-helper`, processus `msedgewebview2.exe` **dont le dossier de données est celui d'EvolutionWin** (sans toucher à Edge ni aux autres applications WebView2)
  - [ ] Aucune boîte de dialogue « fichiers en cours d'utilisation » ni demande de redémarrage (`MSIRESTARTMANAGERCONTROL=Disable`, `REBOOT=ReallySuppress`) ; si un fichier reste verrouillé malgré tout, remplacement au prochain démarrage et code de sortie 3010 au lieu d'un échec — *fait : `RebootPrompt="no"` ; reste : `MSIRESTARTMANAGERCONTROL=Disable`, code 3010*
  - [ ] Sans `FORCECLOSE` en mode interactif : boîte de dialogue proposant de fermer Evolution — *aujourd'hui la fermeture est automatique, sans question*
  - [ ] Journalisation des processus fermés dans le journal MSI (`/l*v`) — *partiel : le journal contient l'action `Wix4CloseApplications` et la liste des programmes visés, pas le détail des processus réellement fermés*
  - [ ] Optionnel : relancer EvolutionWin pour l'utilisateur courant à la fin d'une installation silencieuse (`LAUNCHAPP=1`, utilisé par la mise à jour automatique)
- [ ] 🟡 **Nettoyage des données utilisateur à la désinstallation** (sur demande) — aujourd'hui la désinstallation ne retire que `C:\Program Files\Evolution` et laisse les données de chaque utilisateur
  - [ ] Données concernées : `%LOCALAPPDATA%\EvolutionWin\` (préférences, journaux, WebView2), `%LOCALAPPDATA%\evolution\` (comptes `sources\`, courrier local, carnets, agendas), cache du courrier `%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\`, identifiants `Evolution-Data-Server:*` du Gestionnaire d'identifiants Windows
  - [ ] **Conservées par défaut** (réinstallation sans perte, comme Outlook / Thunderbird) ; suppression seulement avec la propriété `PURGEUSERDATA=1` : `msiexec /x Evolution-<version>-x64.msi PURGEUSERDATA=1`
  - [ ] **Jamais lors d'une mise à jour** : une mise à jour majeure du MSI désinstalle l'ancienne version, le nettoyage doit être exclu dans ce cas (`REMOVE="ALL" AND NOT UPGRADINGPRODUCTCODE`)
  - [ ] Nettoyage du profil de l'utilisateur qui désinstalle (installation par machine : les autres profils ne sont pas accessibles proprement) ; documenter le nettoyage manuel des autres profils
  - [ ] Optionnel : case « Supprimer aussi mes comptes et données » dans l'interface de désinstallation du MSI (non affichée par *Paramètres › Applications installées*)
  - [ ] Journalisation des éléments supprimés dans le journal MSI
- [x] 🟢 **Menu « À propos »** (menu *Aide* / *Fichier*, ou entrée en bas de la barre latérale) : boîte de dialogue avec icône, nom, version en cours, licence (LGPL 2.1 ou ultérieure), lien vers le dépôt GitHub et le [CHANGELOG.md](CHANGELOG.md)
  - [x] Version tirée du fichier [VERSION](VERSION) à la compilation (macro / ressource `VERSIONINFO` générée par MSBuild, comme le fait déjà `build-msi.ps1` pour le MSI), pour que l'exécutable, le MSI et la boîte « À propos » affichent toujours la même version
  - [x] Versions des composants (EDS, evolution-ews, Windows App SDK, WebView2) et bouton « Copier les informations » pour les rapports de bug
- [ ] 🟡 **Mise à jour automatique depuis GitHub** ([pitch7900/Evolution-WindowsPort](https://github.com/pitch7900/Evolution-WindowsPort))
  - [ ] **Vérification de version** : dernière Release lue par l'API `https://api.github.com/repos/pitch7900/Evolution-WindowsPort/releases/latest` (champ `tag_name` pour la version, `assets` pour l'URL du MSI et de son `.sha256`), comparée à la version compilée dans l'exécutable (comparaison numérique `x.y.z`, pas textuelle) ; préversions via `releases` pour le canal « préversions »
  - [ ] Fréquence : au démarrage (après quelques minutes, pour ne pas ralentir la relève) puis une fois par jour ; bouton *Rechercher les mises à jour* dans la boîte « À propos »
  - [ ] **Notification** de la nouvelle version (bandeau / toast) avec les nouveautés extraites du [CHANGELOG.md](CHANGELOG.md), boutons *Installer maintenant*, *Plus tard*, *Ignorer cette version*
  - [ ] **Téléchargement automatique** du MSI joint à la Release GitHub (`Evolution-<version>-x64.msi`) en arrière-plan, avec progression dans la barre d'état (§4), dans `%LOCALAPPDATA%\EvolutionWin\Updates`
  - [ ] **Vérification d'intégrité avant installation** : empreinte SHA-256 publiée à côté du MSI (`.sha256`) et signature Authenticode du MSI (`WinVerifyTrust`, éditeur attendu) ; refus d'installer si la vérification échoue
  - [ ] **Installation** : fermeture propre de l'application (boîte d'envoi vidée, synchronisation terminée), lancement de `msiexec /i … /passive` (élévation UAC, installation par machine), redémarrage automatique d'EvolutionWin à la fin
  - [ ] Nettoyage des MSI téléchargés après installation réussie
  - [ ] **Paramètres** (§19) : mise à jour automatique / notification seulement / désactivée ; canal (versions stables, préversions)
  - [ ] **Politique d'entreprise** : désactivation par clé de registre machine ou propriété MSI (`AUTOUPDATE=0`) pour les déploiements gérés (GPO / Intune)
  - [x] Évolution : passer aux *Releases* GitHub (cf. point suivant) plutôt que `dist/` dans le dépôt
- [x] 🟡 **Publication des versions par les Releases GitHub** (au lieu de versionner les MSI dans `dist/`, ce qui alourdit l'historique Git à chaque version)
  - [x] Une *Release* par version, avec l'étiquette Git `v<version>` (ex. `v0.9.3`) créée à partir du fichier [VERSION](VERSION)
  - [x] Fichiers joints à la Release : `Evolution-<version>-x64.msi` et son empreinte `.sha256`
  - [x] Notes de version reprises automatiquement de la section correspondante du [CHANGELOG.md](CHANGELOG.md)
  - [x] Script de publication (`tools\publish-release.ps1`, par l'API REST GitHub plutôt que `gh`, non installé ; jeton des identifiants de Git) lancé après `build-msi.bat` : vérifie que la version n'existe pas déjà, que le CHANGELOG contient la version, puis crée l'étiquette et la Release
  - [x] Préversions marquées *Pre-release* (canal « préversions » de la mise à jour automatique)
  - [x] Retirer `dist/` du dépôt (et l'ajouter au `.gitignore`), mettre à jour la documentation ([WINDOWS-PORT.md](WINDOWS-PORT.md), README)
  - [ ] 🟠 Optionnel : construction et publication automatiques par GitHub Actions à chaque étiquette (nécessite un runner Windows avec toute la chaîne MSVC / gvsbuild, ou un runner auto-hébergé)

## 2. Interface générale et disposition — 🟡

### Structure de la fenêtre

- [x] 🟡 **Barre d'outils à onglets façon ruban** pour les vues Mail et Calendrier : *Fichier*, *Accueil*, *Envoi/réception*, *Dossier*, *Affichage* (WinUI n'a pas de ruban : `TabView` / `SelectorBar` + `CommandBar`). Hors périmètre : intégrations tierces (Teams, OneNote, TeamViewer, Rainbow, Copilot)
- [x] 🟡 **Barre d'accès rapide** personnalisable dans la barre de titre (Envoyer/Recevoir, Annuler, etc.) — *reste : « Annuler » (pas encore de mécanisme d'annulation)*
- [x] 🟢 **Barre latérale gauche** de navigation entre modules (Mail, Calendrier, Contacts, Tâches) — `NavigationView` déjà en place, à restyler en barre d'icônes
- [x] 🟢 **Bandeau d'information** sous la barre d'outils (`InfoBar`), affiché dans les vues Mail et Calendrier (ex. réponses automatiques actives + bouton *Désactiver*)

### Disposition des panneaux

- [x] 🟢 **Panneaux redimensionnables** : séparateurs déplaçables entre dossiers, liste des messages et volet de lecture ; tailles mémorisées entre deux sessions
- [ ] 🟡 **Position du volet de lecture** (menu *Affichage › Volet de lecture*), en Mail comme en Calendrier : <a id="demo-volet-lecture"></a> — *fait en Mail ; le Calendrier n'a pas encore de volet de lecture (§12)*
  - [x] 🟢 À droite
  - [x] 🟢 En bas — *hauteur liste / lecture fixe, pas encore réglable à la souris*
  - [ ] 🟢 Désactivé (*grisé dans le menu en attendant la fenêtre de lecture indépendante*) : ouverture des messages dans la fenêtre de lecture indépendante (§6) et des événements dans leur fenêtre
  - [x] 🟢 Choix mémorisé dans les préférences
- [ ] 🟢 **Volet des dossiers** : normal / réduit / désactivé, en Mail comme en Calendrier (en Calendrier, le mode réduit garde le mini-calendrier)
- [ ] 🟢 **Espacement plus étroit** (mode compact) des listes, en Mail comme en Calendrier
- Barre des tâches à droite (To-Do bar) : cf. « Barre latérale des tâches », §16

### Fenêtres et lecture

- [ ] 🟡 **Ouvrir un événement ou un dossier dans une nouvelle fenêtre** (pour un message : fenêtre de lecture indépendante, §6) ; 🟢 **Fermer tous les éléments ouverts**
- [ ] 🟠 **Fenêtre des rappels** : rappels des rendez-vous (alarmes EDS) et des messages en suivi, avec Reporter / Ignorer
- [ ] 🟡 **Lecteur immersif** : vue de lecture plein écran épurée (taille du texte, espacement)
- [ ] 🟢 **Lecture à voix haute** du message (`Windows.Media.SpeechSynthesis`)

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
- [ ] 🟡 **Connexion Gmail / Google Workspace compatible avec la double authentification (2FA)** — *bogue actuel : « identifiants invalides » même avec le bon mot de passe* <a id="demo-gmail-2fa"></a>
  - [ ] Cause : Google refuse la connexion IMAP / SMTP par mot de passe du compte (« applications moins sécurisées » supprimées) ; seuls OAuth2 (XOAUTH2) ou un *mot de passe d'application* fonctionnent. Aujourd'hui, l'assistant laisse saisir le mot de passe du compte Google, et OAuth2 n'est utilisable que si l'on fournit soi-même un *Client ID* Google Cloud (`EVO_OAUTH2_CLIENT_ID`)
  - [ ] **OAuth2 par défaut pour Gmail**, sans champ mot de passe : bouton *Se connecter avec Google* ; la double authentification (SMS, application Google, clé de sécurité, passkey) est alors gérée par la page de connexion Google elle-même
  - [ ] **Connexion dans le navigateur système** (redirection en boucle locale `http://127.0.0.1:<port>`, PKCE, RFC 8252) et non dans un WebView2 intégré : Google bloque la connexion depuis les navigateurs intégrés (« Ce navigateur ou cette application ne sont peut-être pas sécurisés », `disallowed_useragent`) — à vérifier aussi pour Microsoft 365
  - [ ] **Client ID Google fourni avec l'application** (projet Google Cloud d'EvolutionWin, application de bureau, portées `https://mail.google.com/` + Calendar + Contacts pour §3 « Calendrier et contacts ») — ou réutiliser le service OAuth2 Google d'EDS (`module-oauth2-services`) s'il est compilé avec un identifiant ; vérification de l'application par Google nécessaire pour dépasser 100 utilisateurs de test (portées sensibles)
  - [ ] **Repli « mot de passe d'application »** (compte avec 2FA, OAuth2 indisponible) : champ dédié avec explication et lien vers `https://myaccount.google.com/apppasswords`
  - [ ] Message d'erreur explicite si Google refuse le mot de passe (« Google exige une connexion sécurisée : utilisez *Se connecter avec Google* ou un mot de passe d'application ») au lieu de « identifiants invalides »
  - [ ] Jeton de rafraîchissement enregistré dans le Gestionnaire d'identifiants Windows et renouvelé automatiquement (mécanisme XOAUTH2 existant d'`evo-core`) ; jeton révoqué → *Se reconnecter* (§4)
- [ ] 🟡 **Simplifier l'assistant d'ajout de compte** — aujourd'hui la seconde étape affiche tous les champs à la fois (type, nom du compte, nom complet, adresse, utilisateur, serveur EWS, serveurs et ports IMAP / SMTP, Client ID et Tenant ID OAuth2, mot de passe), même quand l'autodétection a tout trouvé <a id="demo-assistant-compte"></a>
  - [ ] **Écran principal réduit à l'essentiel** : adresse électronique, nom complet, puis selon le type *Mot de passe* ou bouton *Se connecter avec Microsoft / Google* (OAuth2) — rien d'autre quand la détection a réussi
  - [ ] **Résumé lisible de la détection** à la place des champs techniques : logo / nom du fournisseur et une phrase (« Compte Microsoft 365 détecté — connexion sécurisée par Microsoft », « Serveurs trouvés : imap.exemple.ch, smtp.exemple.ch (chiffrés) »), avec la source de la détection (base Thunderbird, Autodiscover, DNS…) en info-bulle
  - [ ] **Section repliée « Paramètres avancés »** (`Expander`, fermé par défaut) regroupant : type de compte forcé, nom du compte, nom d'utilisateur (si différent de l'adresse), serveurs et ports entrants / sortants, chiffrement (TLS / STARTTLS en liste plutôt qu'en numéro de port), méthode d'authentification, URL EWS, Client ID / Tenant ID OAuth2
  - [ ] Section avancée **ouverte automatiquement** seulement si la détection a échoué ou n'a donné que des valeurs supposées (`imap.<domaine>` / `smtp.<domaine>`), avec les champs à vérifier mis en évidence
  - [ ] Nom du compte prérempli avec l'adresse (modifiable dans les paramètres avancés ou plus tard dans la page du compte, §19)
  - [ ] **Test de connexion** avant création (*Vérification…* puis coche verte, ou message d'erreur compréhensible et ouverture des paramètres avancés sur le champ fautif)
  - [ ] Libellés sans jargon (« Serveur de réception » plutôt que « Serveur entrant (IMAP) », chiffrement en clair plutôt que « 993 = TLS, 143 = STARTTLS »)
  - [ ] Une seule fenêtre à étapes (adresse → connexion → terminé) plutôt que deux boîtes de dialogue successives ; bouton *Précédent*

### Modifier un compte existant

Aujourd'hui, la liste des comptes des Paramètres ne propose que *Définir par défaut* et *Supprimer* : pour corriger un serveur ou un mot de passe, il faut supprimer puis recréer le compte.

- [ ] 🟡 **Bouton *Modifier*** (et clic sur le compte) ouvrant la page du compte (une page par compte dans les Paramètres, §19), avec les mêmes sections que l'assistant d'ajout : essentiel en haut, *Paramètres avancés* repliés <a id="demo-modifier-compte"></a>
- [ ] 🟢 **Identité** : nom affiché, nom complet, adresse de réponse (Reply-To), organisation, nom du compte dans l'arborescence
- [ ] 🟡 **Serveurs** : serveurs, ports, chiffrement, nom d'utilisateur et méthode d'authentification de réception et d'envoi (modification des sources EDS `ESourceMailAccount` / `ESourceMailTransport` / `ESourceMailIdentity` puis reconnexion des services Camel sans redémarrer l'application)
- [ ] 🟢 **Mot de passe** : *Changer le mot de passe* (Gestionnaire d'identifiants Windows) ; pour OAuth2 : *Se reconnecter* (nouveau jeton) et affichage du compte Microsoft / Google connecté
- [ ] 🟢 **Dossiers spéciaux** : choix des dossiers Envoyés, Brouillons, Corbeille, Indésirables, Archive sur le serveur
- [ ] 🟢 **Rédaction** : signature par défaut (nouveaux messages / réponses), copie cachée automatique, demande d'accusé de lecture
- [ ] 🟡 **Synchronisation** : intervalle de relève propre au compte (remplace la valeur globale des Paramètres, §19), dossiers synchronisés hors connexion et ancienneté (§4), compte activé / désactivé sans le supprimer
- [ ] 🟡 **Test de connexion** après modification, avec retour à l'ancienne configuration en cas d'échec si l'utilisateur le souhaite
- [ ] 🟢 Actions *Définir par défaut* et *Supprimer* déplacées dans cette page (avec confirmation pour la suppression), en plus du menu de la liste

### Compte par défaut

- [x] 🟢 **Désigner le compte par défaut** dans la liste des comptes, avec indication visuelle (`e_source_registry_set_default_mail_account`)
- [x] 🟢 Utilisé pour les nouveaux messages ; pour une réponse / un transfert, compte ayant reçu le message (repli sur le compte par défaut)
- [ ] 🟢 Calendrier et carnet d'adresses par défaut pour les nouveaux événements et contacts — *les nouveaux éléments vont déjà dans le calendrier / carnet par défaut d'EDS ; reste : les choisir dans l'interface*
- [ ] 🟡 Gestionnaire des liens `mailto:` (inscription d'EvolutionWin comme application de messagerie Windows). Facultatif au moment de l'installation MSI et dans les menus de configuration de l'application par la suite.

### Signatures — 🟡

- [x] 🟡 **Éditeur de signatures** (texte et HTML : mise en forme, images, liens) dans les Réglages — réutilise l'éditeur WebView2 de la rédaction ; sources EDS `ESourceMailSignature`
- [x] 🟢 Créer / modifier / supprimer plusieurs signatures nommées
- [x] 🟢 Par compte : signature des nouveaux messages et signature des réponses / transferts (ou aucune)
- [x] 🟡 Changement de signature dans la fenêtre de rédaction, mise à jour automatique au changement de compte expéditeur
- [x] 🟢 Placement (au-dessus / en dessous du texte cité)

### Réponses automatiques (Out of Office) — 🟠

- [ ] **Gestion des réponses automatiques** (*Fichier › Réponses automatiques* et menu contextuel du compte) : <a id="demo-reponses-auto"></a>
  - [ ] 🟡 Exchange on-premise (EWS `GetUserOofSettings` / `SetUserOofSettings`, déjà présents dans evolution-ews)
  - [ ] 🟡 Microsoft 365 (Graph `mailboxSettings/automaticRepliesSetting`)
  - [ ] 🟠 Gmail / Google Workspace (API Gmail *vacation settings*, nouvelle portée OAuth2)
  - [ ] 🟠 IMAP avec Sieve / ManageSieve (extension `vacation` ; client ManageSieve à écrire)
  - [ ] 🟢 Option grisée pour les comptes qui ne la supportent pas
- [ ] **Options** :
  - [ ] 🟢 Activer / désactiver ; plage de dates ou durée indéterminée
  - [ ] 🟢 Messages distincts interne / externe (éditeur HTML)
  - [ ] 🟢 Externe : tous les expéditeurs ou seulement les contacts
  - [ ] 🟡 Exchange / M365 : bloquer le calendrier, refuser les nouvelles invitations, annuler les réunions de la période
- [ ] 🟢 **Indicateur visible** : bandeau « Réponses automatiques activées pour ce compte » + *Désactiver* ; rappel à l'ouverture si la période est expirée

### Informations du compte (onglet Fichier)

- [ ] 🟡 Quota du compte (`camel_folder_get_quota_info_sync`) et outils de nettoyage de la boîte aux lettres
- Paramètres du compte : cf. « Modifier un compte existant » ci-dessus ; options générales : cf. §19

### Calendrier et contacts des comptes Microsoft 365 et Gmail

Objectif : retrouver dans EvolutionWin le calendrier et les contacts du compte en ligne de l'utilisateur, et pouvoir les gérer simplement (les fonctions avancées restent aux §12 à §15). Aujourd'hui : un compte Microsoft 365 crée déjà une collection EDS avec calendrier et contacts activés (`e_source_collection_set_calendar_enabled` / `_contacts_enabled`), sans validation de bout en bout ; un compte Gmail n'est créé qu'en IMAP / SMTP (XOAUTH2), sans calendrier ni contacts ; l'interface sait lister, créer et supprimer des événements et des contacts, mais pas les modifier.

- [ ] 🟡 **Microsoft 365 / Exchange Online** : vérifier de bout en bout que la collection `microsoft365` (Graph) découvre et synchronise les calendriers et carnets d'adresses du compte ; les afficher sous le nom du compte dans les vues Calendrier et Contacts <a id="demo-cal-contacts-m365"></a>
  - [ ] Portées OAuth2 nécessaires (`Calendars.ReadWrite`, `Contacts.ReadWrite`) demandées à la connexion ; consentement manquant → message clair et *Se reconnecter*
  - [ ] Exchange on-premise (collection `ews`) : même vérification dès qu'un serveur de test est disponible
- [ ] 🟠 **Gmail / Google Workspace** : à l'ajout d'un compte Gmail, créer aussi une collection Google (backend `google` d'EDS : calendriers CalDAV, contacts CardDAV / Google People) avec le même jeton OAuth2 <a id="demo-cal-contacts-gmail"></a>
  - [ ] Portées OAuth2 Calendar et Contacts ajoutées à la connexion Google (en plus de la messagerie)
  - [ ] Comptes Gmail déjà configurés : bouton *Activer le calendrier et les contacts* dans la page du compte (nouvelle connexion pour accorder les portées)
- [ ] 🟡 **Gestion simple du calendrier du compte** : <a id="demo-gestion-calendrier"></a>
  - [x] Événements du calendrier principal (et des autres calendriers du compte) affichés avec la couleur du calendrier
  - [x] Créer un événement dans le calendrier choisi (calendrier principal du compte par défaut) : titre, lieu, date, heures ou journée entière, description
  - [x] **Modifier** un événement existant (manquant aujourd'hui) et le supprimer, avec envoi au serveur — *clic sur l'événement : fenêtre modifiable (lecture seule si l'agenda l'est), organisateur et participants*
  - [ ] Modifications faites ailleurs (Outlook Web, Google Agenda, téléphone) visibles à la synchronisation suivante ; bouton *Actualiser*
  - [x] Événements périodiques existants affichés correctement (occurrences développées), en lecture seule tant que l'éditeur complet (§13) n'existe pas
- [ ] 🟡 **Gestion simple des contacts du compte** : <a id="demo-gestion-contacts"></a>
  - [x] Contacts du compte (et sous-dossiers / groupes de contacts) listés avec le nom du carnet d'origine — *volet « Carnets d'adresses » par compte, cases à cocher mémorisées, carnet actif*
  - [x] Créer un contact dans le carnet choisi (carnet du compte par défaut) : nom, prénom, adresses email, téléphones, société, fonction, notes
  - [ ] **Modifier** un contact existant (manquant aujourd'hui) et le supprimer, avec envoi au serveur
  - [ ] Contacts utilisables dans l'autocomplétion des destinataires (§15)
- [ ] 🟢 Erreurs de synchronisation du calendrier / des contacts remontées dans l'indicateur d'état des connexions (§4)

## 4. Synchronisation et mode hors connexion — 🟠

### Progression

- [ ] 🟡 **Barre de progression dans la barre d'état** lors d'un rafraîchissement (messages de progression `CamelOperation` relayés par `evo-core`) : <a id="demo-progression"></a>
  - [ ] Texte de l'opération en cours (« Compte X – Boîte de réception : chargement des en-têtes 120 / 850 »)
  - [ ] Pourcentage quand le total est connu, indéterminée sinon
  - [ ] Progression globale pour plusieurs comptes / dossiers en parallèle
  - [ ] 🟡 Bouton *Annuler* / *Annuler tout* (`GCancellable`)
  - [ ] Masquage automatique à la fin, message bref en cas d'erreur
- [ ] 🟢 **Progression sur l'icône de la barre des tâches Windows** (`ITaskbarList3`, rouge en cas d'erreur)

### Envoi et réception

- [x] 🟢 **Envoyer/Recevoir tous les dossiers** (F9 ; F5 existe déjà) et **Mettre à jour le dossier courant** (Maj+F9)
- [ ] 🟠 **Boîte d'envoi et Envoyer tout** : messages mis en file d'attente au lieu d'être envoyés directement (prérequis du mode hors connexion)
  - [ ] Dossier local *Boîte d'envoi* (stockage local Camel), visible dans l'arborescence avec le nombre de messages en attente
  - [ ] Envoi automatique de la file dès que le réseau revient (et à chaque relève), dans l'ordre de création
  - [ ] Message en attente modifiable ou supprimable tant qu'il n'est pas parti ; envoi différé (« Envoyer à… ») optionnel
  - [ ] Erreurs d'envoi : message conservé dans la boîte d'envoi avec l'erreur affichée, nouvelle tentative automatique avec délai croissant
- [ ] 🟠 **Groupes d'envoi/réception** : groupes de comptes / dossiers avec planification (intervalle, à la fermeture, hors connexion)

### Hors connexion et téléchargement

- [ ] 🟠 **Cache local complet pour le mode hors connexion** : messages (en-têtes, corps et pièces jointes), contacts, calendriers et tâches disponibles sans réseau
  - [ ] Synchronisation hors connexion des dossiers (réglage IMAP `stay-synchronized` / `sync-offline` de Camel), par compte et par dossier, avec limite d'ancienneté (ex. 3 derniers mois) et de taille
  - [ ] Caches EDS des carnets d'adresses et calendriers (backends en cache local : EWS, CalDAV, CardDAV) consultables hors ligne
  - [ ] Recherche, lecture, aperçu des pièces jointes et rédaction fonctionnels hors ligne
  - [ ] Actions faites hors ligne (lu / non lu, drapeau, déplacement, suppression, événements, contacts) journalisées et rejouées au retour du réseau ; résolution des conflits
- [ ] 🟠 **Travailler en mode hors connexion** : bascule manuelle globale (`CamelOfflineStore`) ; envois mis en file d'attente dans la boîte d'envoi
- [ ] 🟡 **Détection automatique de l'état du réseau** : surveillance de la connectivité (`GNetworkMonitor` / `NetworkInformation.NetworkStatusChanged`) et vérification régulière de l'accès aux serveurs des comptes (ex. toutes les 30 s hors ligne) ; passage automatique hors ligne / en ligne, puis relève et envoi de la boîte d'envoi au retour
- [ ] 🟡 **Indicateur d'état des connexions** dans la barre d'état (en bas), qui sert aussi d'indicateur du mode hors connexion — icône verte : tout est connecté ; orange : connexion en cours / partielle ; rouge : erreur ; grise : hors connexion <a id="demo-etat-connexions"></a>
  - [ ] 🟢 Hors connexion : message « Hors connexion — N messages en attente d'envoi », boutons *Réessayer maintenant* / *Travailler en ligne*
  - [ ] Clic : panneau listant chaque compte et chacun de ses services (IMAP, SMTP, Exchange / EWS, Microsoft Graph, Gmail, CalDAV / CardDAV…) avec état, serveur, dernière synchronisation réussie
  - [ ] Message d'erreur explicite en cas d'échec : serveur injoignable, certificat refusé, délai dépassé, quota, **authentification requise**
  - [ ] **Réauthentification** mise en évidence (mot de passe changé, jeton OAuth2 expiré ou révoqué, changement de compte / de tenant, MFA) : bouton *Se reconnecter* qui relance la saisie du mot de passe ou la connexion OAuth2 sans passer par les Paramètres
  - [ ] Bouton *Réessayer* par compte ; lien vers les paramètres du compte
  - [ ] Pastille d'erreur sur le compte concerné dans l'arborescence des dossiers et notification Windows (une seule fois par erreur) quand une réauthentification est nécessaire
  - [ ] État fourni par `evo-core` (événement `connection-status` par service, à partir de `CamelService:connection-status`, des erreurs de relève et de l'état des sources EDS)
- [ ] 🟡 **Préférences de téléchargement** : en-têtes seuls / message complet, limite de taille (synchronisation hors connexion des dossiers : cf. « Cache local » ci-dessus)
- [ ] 🟠 **Mode en-têtes seuls** : télécharger les en-têtes, marquer pour téléchargement (message / copie), annuler les marques, traiter les en-têtes marqués

## 5. Mail – Liste des messages — 🟠

### Tri et colonnes

- [x] 🟡 **Trier les emails** via la ligne « Trier par » au-dessus de la liste et *Affichage › Trier par* (la `ListView` actuelle n'a pas d'en-têtes de colonnes) :
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
  - [ ] Avec indicateur, avec pièces jointes, cette semaine, importants, mentionné — *fait : indicateur, pièces jointes, cette semaine ; reste : importants et mentionné (importance et destinataires absents des résumés transmis par `evo-core`)*
  - [ ] Combinables avec la recherche et fonctionnels dans les vues unifiées — *fait : combinés avec la recherche ; reste : vues unifiées (§11)*

### Conversations

- [ ] 🟠 **Grouper par discussion** (*Affichage › Afficher sous forme de conversations*) : <a id="demo-conversations"></a>
  - [ ] Regroupement par `Message-ID` / `In-Reply-To` / `References` (`CamelFolderThread`), repli sur l'objet normalisé « RE: / TR: »
  - [ ] Fil réductible / développable, nombre de messages et de non lus
  - [ ] 🟠 Inclure les messages d'autres dossiers (réponses envoyées)
  - [ ] 🟢 Choix mémorisé par dossier
- [ ] 🟡 **Paramètres de conversation** : expéditeurs au-dessus de l'objet, toujours développer la conversation sélectionnée, retrait classique

### Boîte de réception Prioritaire

- [ ] 🟠 **Onglets *Prioritaire* / *Autres*** (classement Exchange Online `inferenceClassification` via Graph ; sans objet pour les autres comptes)

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
  - [ ] Clic sur une pièce jointe → aperçu à la place du corps du message, bouton *Retour au message*, navigation entre pièces jointes
  - [ ] **Rendu sécurisé, sans exécution de code actif** :
    - [ ] 🟢 Images (PNG, JPEG, GIF, BMP, WebP, SVG converti en image fixe) : décodage natif (`BitmapImage` / WIC), SVG sans scripts
    - [ ] 🟢 Texte, CSV, JSON, XML, code : affichage en texte brut (jamais interprété)
    - [ ] 🟡 PDF : rendu page par page en images avec `Windows.Data.Pdf` (aucun JavaScript, formulaire, lien de lancement ni pièce jointe imbriquée exécutés), zoom et défilement
    - [ ] 🟡 HTML / EML / MSG joint : WebView2 isolé avec scripts désactivés et contenu distant bloqué (même règles que le lecteur, §6)
    - [ ] 🟠 Office (Word, Excel, PowerPoint, OpenDocument) : rendu en lecture seule **sans macros ni contenu actif** — conversion en PDF / image hors processus, ou gestionnaires d'aperçu Windows (`IPreviewHandler`) exécutés dans un processus isolé à faibles privilèges ; macros VBA, objets OLE / ActiveX, liens externes et DDE jamais exécutés
    - [ ] 🟢 Types non pris en charge ou à risque (`.exe`, `.js`, `.vbs`, `.ps1`, `.lnk`, `.hta`, archives…) : pas d'aperçu, simple fiche (nom, type, taille) avec avertissement
  - [ ] 🟡 Fichiers d'aperçu temporaires dans un dossier privé, supprimés à la fermeture ; taille maximale configurable pour l'aperçu
  - [ ] 🟢 Boutons *Enregistrer* et *Ouvrir avec l'application* (avec avertissement si le fichier vient d'Internet : marque *Mark of the Web* `Zone.Identifier` posée à l'enregistrement)
- [ ] 🟡 **Double-clic sur un mail : ouverture dans une fenêtre indépendante** (aussi avec Entrée et *Ouvrir* du menu contextuel) <a id="demo-fenetre-message"></a>
  - [ ] Fenêtre de lecture avec en-têtes, pièces jointes, bandeau de sécurité / contenu externe et barre d'actions (Répondre, Répondre à tous, Transférer, Supprimer, Déplacer, Lu / Non lu, Imprimer)
  - [ ] Navigation message précédent / suivant dans la fenêtre
  - [ ] Plusieurs fenêtres ouvertes simultanément ; taille et position mémorisées
  - [ ] Brouillon (dossier Brouillons) : double-clic ouvre la fenêtre de rédaction pour reprendre le message
  - [ ] Message marqué comme lu à l'ouverture

## 7. Mail – Actions sur les messages — 🟠

### Créer et répondre

- [x] 🟢 **Nouveaux éléments** (menu) : courrier, rendez-vous, réunion, contact, tâche <a id="demo-nouveaux-elements"></a> — *« Réunion » grisée en attendant les invitations (§13)*
- [ ] 🟡 **Brouillon en enregistrement automatique (autosave)** pendant la rédaction (nouveau message, réponse, transfert) : l'utilisateur n'a jamais à penser à enregistrer — *existant : le dossier Brouillons est déjà reconnu (rôle `CAMEL_FOLDER_TYPE_DRAFTS` ou dossier configuré dans l'identité EDS) ; aucun enregistrement de brouillon aujourd'hui*
  - [ ] **Autosave** : premier enregistrement dès que le message est modifié (destinataire, objet ou texte saisi), puis à chaque pause de frappe (ex. 3 s sans saisie), au plus tard toutes les 30 s pendant une saisie continue, à chaque ajout / retrait de pièce jointe, à la perte de focus et à la fermeture de la fenêtre ; aucun enregistrement si rien n'a changé depuis le précédent
  - [ ] Enregistrement en arrière-plan, sans bloquer la saisie ni voler le focus ; échec (serveur injoignable) signalé discrètement et nouvelle tentative au cycle suivant
  - [ ] Ctrl+S reste disponible pour forcer l'enregistrement immédiat
  - [ ] Brouillon écrit dans le dossier **Brouillons du compte expéditeur** (`evo_core_save_draft` : message MIME complet avec pièces jointes, en-têtes `In-Reply-To` / `References` conservés, indicateur `CAMEL_MESSAGE_DRAFT`) ; chaque enregistrement **remplace** la version précédente (pas de doublons)
  - [ ] **Création du dossier Brouillons** s'il n'existe pas encore sur le serveur (`camel_store_create_folder_sync`), puis inscription dans l'identité du compte (`e_source_mail_composition_set_drafts_folder`) ; repli sur un dossier Brouillons local si le serveur refuse ou si l'on est hors connexion, avec transfert au retour du réseau (§4)
  - [ ] Changement de compte expéditeur pendant la rédaction : brouillon déplacé vers le dossier Brouillons du nouveau compte
  - [ ] À l'envoi : brouillon supprimé du dossier Brouillons ; à la fermeture sans envoi : **pas de question**, le brouillon est déjà enregistré (dernier autosave à la fermeture) ; bouton *Supprimer le brouillon* dans la fenêtre de rédaction pour abandonner le message
  - [ ] Reprise d'un brouillon : double-clic dans le dossier Brouillons → fenêtre de rédaction pré-remplie (cf. §6), compte, signature et format (HTML / texte) restaurés
  - [ ] Indication discrète de l'état de l'autosave dans la fenêtre de rédaction (« Enregistrement… », « Brouillon enregistré à 14:32 », « Non enregistré — nouvelle tentative »)
  - [ ] Récupération après plantage : copie locale écrite à chaque autosave, brouillons non synchronisés retrouvés au redémarrage
- [ ] 🟡 **Répondre par une réunion** (réunion pré-remplie avec les participants) — dépend de §13
- [ ] 🟢 **Transférer en pièce jointe** pour un message seul (déjà fait en sélection multiple)

### Supprimer, archiver, déplacer

- [x] 🟢 **Archiver** : déplacer vers le dossier d'archive du compte (réglage `archive-folder` d'EDS), touche Retour arrière <a id="demo-archiver"></a> — *dossier Archive du compte (rôle détecté), créé au premier usage s'il n'existe pas*
- [x] 🟢 **Copier vers un dossier** ; liste des derniers dossiers utilisés dans *Déplacer*
- [ ] 🟠 **Ignorer la conversation** : les nouveaux messages du fil vont directement à la Corbeille
- [ ] 🟠 **Nettoyer la conversation** : supprimer les messages redondants (entièrement cités dans un message plus récent)

### Indicateurs et catégories

Trois notions distinctes, comme dans Outlook : le **suivi** (drapeau avec libellé et échéance : ce que *je* dois faire), l'**importance** (fixée par l'expéditeur) et les **catégories** (classement en couleur, §17).

- [ ] 🟡 **Suivi des messages** (évolution du drapeau simple existant) :
  - [ ] **Libellé du suivi** : Urgent, À traiter, À répondre, Pour information, En attente, et libellés personnalisés
  - [ ] **Échéance** : aujourd'hui, demain, cette semaine, semaine prochaine, sans date, date personnalisée
  - [ ] Choix depuis le menu contextuel, le ruban et des raccourcis clavier
  - [ ] Icône et couleur distinctes dans la liste des messages (ex. rouge pour Urgent, orange pour À traiter, échéance dépassée en rouge) et libellé + échéance dans l'en-tête du volet de lecture
  - [ ] Stockage dans les indicateurs Camel (`follow-up`, `due-by`, `completed-on`), synchronisé avec le serveur (mots-clés IMAP, `flag` Exchange / Graph)
  - [ ] *Marquer comme terminé* en un clic (avec date de fin), *Effacer l'indicateur*
  - [ ] Filtres rapides « Urgents » et « À traiter » (§5) et dossiers de recherche correspondants (§9)
  - [ ] 🟠 Rappel sur un suivi — dépend de la fenêtre des rappels (§2)
- [ ] 🟡 **Importance** (haute / normale / basse) : affichée dans la liste et le volet de lecture (en-têtes `Importance` / `X-Priority`), choisie à la rédaction ; filtre rapide « Importants » (§5)
- Catégoriser le message : cf. §17 ; créer une tâche à partir du message : cf. §16

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

- [ ] 🟡 **Nouveau dossier, Renommer, Copier, Déplacer, Supprimer** (`camel_store_create_folder_sync`, etc.), glisser-déposer dans l'arborescence — *fait : menu contextuel (nouveau, renommer, déplacer, copier, supprimer) ; reste : glisser-déposer*
- [x] 🟢 **Marquer tout comme lu**
- [x] 🟢 **Vider le dossier** : vider le dossier (vers la Corbeille), la Corbeille, les Indésirables
- [ ] 🟡 **Réorganiser l'ordre des dossiers** d'un compte dans l'arborescence (aujourd'hui triés par ordre alphabétique) : <a id="demo-dossiers"></a>
  - [x] 🟢 **Ordre par défaut « dossiers spéciaux d'abord »**, comme Outlook : Boîte de réception, Brouillons, Éléments envoyés, Éléments supprimés (Corbeille), Archive, Courrier indésirable, Boîte d'envoi, puis les autres dossiers par ordre alphabétique — dossiers spéciaux reconnus par leur rôle (`CAMEL_FOLDER_TYPE_*`, attributs IMAP SPECIAL-USE, dossiers distinctifs Exchange) et non par leur nom, quelle que soit la langue du serveur — *rôle du serveur, puis dossiers configurés dans le compte (Envoyés, Brouillons, Archive, Corbeille / Indésirables réels), en dernier recours nom usuel pour un rôle encore libre*
  - [x] 🟢 **Icône de la Boîte de réception** : remplacer le glyphe plein `\xE8A8` (enveloppe pleine, rendue comme un carré noir à cette taille) par une icône au trait cohérente avec les autres dossiers (ex. `\xE715`, enveloppe) — `FolderGlyph()` dans [MainWindow.xaml.cpp](winui/app/MainWindow.xaml.cpp) ; vérifier aussi `inbox_glyph`, qui sert à repérer le nœud Boîte de réception par son glyphe
  - [x] 🟢 Noms des dossiers spéciaux affichés dans la langue de l'interface (« INBOX » → « Boîte de réception », « Sent Items » → « Éléments envoyés »)
  - [ ] 🟡 **Ordre personnalisé** par glisser-déposer (ou *Monter* / *Descendre* dans le menu contextuel), mémorisé par compte, sans modifier les dossiers sur le serveur
  - [ ] 🟢 Choix du mode de tri par compte : dossiers spéciaux d'abord (défaut) / alphabétique (*Afficher tous les dossiers de A à Z*) / personnalisé ; *Rétablir l'ordre par défaut*
  - [ ] 🟢 Ordre des **comptes** eux-mêmes dans le volet des dossiers (compte par défaut en premier, puis glisser-déposer)
- [ ] 🟠 **Nettoyer le dossier** (et sous-dossiers) des messages redondants — même logique que §7
- [ ] **Récupérer les éléments supprimés** :
  - [ ] 🟡 IMAP : messages marqués supprimés non purgés
  - [ ] 🟠 Exchange : *Recoverable Items*
- [ ] 🟡 **Favoris** : section *Favoris* en haut du volet des dossiers, réordonnable
- [ ] 🟢 **Afficher dans le navigateur** (OWA / webmail du fournisseur)
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

- [x] 🟢 **Champ de recherche dans la barre de titre** de la fenêtre (au lieu de la vue Mail seule) <a id="demo-recherche-titre"></a> — *dossier courant en Courrier, contacts en Contacts ; barre de titre haute (48 px)*
- [x] 🟢 Bouton pour effacer la recherche ; raccourci Ctrl+E — *et Échap*
- [ ] 🟡 **Portée** : dossier courant / sous-dossiers / compte courant / tous les comptes <a id="demo-recherche-portee"></a>
- [ ] 🟢 **Recherche rapide de contacts** (champ dédié dans le ruban, comme Outlook) et accès au Carnet d'adresses
- [ ] 🟡 **Recherche dans le calendrier** (titre, lieu, description, participants)

### Recherche avancée

- [ ] 🟡 Accessible depuis le champ de recherche simple ; critères combinables : expéditeur, destinataire, objet, corps, plage de dates, taille, pièces jointes, lu / non lu, indicateur, catégorie
- [ ] 🟡 Opérateurs ET / OU entre les critères (expressions de recherche Camel, traduites vers la recherche serveur quand elle existe — cf. « Recherche côté serveur » ci-dessus)
- [ ] 🟠 Enregistrer une recherche comme dossier de recherche — dépend de §9

## 11. Vues unifiées multi-comptes — 🟠

- [ ] 🟠 **Mails** : Boîte de réception unifiée (et Envoyés, Brouillons, Corbeille), compte d'origine indiqué (colonne ou pastille de couleur) — dossiers virtuels Camel
- [ ] 🟡 **Calendriers** : événements de tous les calendriers de tous les comptes, une couleur par calendrier, chacun masquable — *fait dans la vue Calendrier : volet « Calendriers » par compte, cases à cocher mémorisées, couleur par calendrier*
- [ ] 🟡 **Contacts** : carnet d'adresses unifié, détection et regroupement des doublons (même email)
- [ ] 🟠 **Recherche dans les vues unifiées** (simple et avancée) pour mails, événements et contacts, compte d'origine visible dans les résultats
- [ ] 🟢 **Choix des comptes inclus** dans les vues unifiées (préférences)

## 12. Calendrier – Vues et navigation — 🔴

La vue actuelle se limite à un `CalendarView` mensuel et une liste d'événements.

### Grille calendrier (brique préalable)

- [x] 🔴 **Contrôle grille Jour / Semaine de travail / Semaine** (WinUI n'en fournit pas) : créneaux horaires, événements positionnés et chevauchements, journée entière en haut, défilement vertical <a id="demo-grille-calendrier"></a> — *blocs à la couleur du calendrier, ligne de l'heure actuelle, ouverture au clic, vue Liste conservée*
- [x] 🟠 **Vue Mois** (détails faibles / moyens / élevés) — *six semaines, 3 événements par jour puis « +N », clic sur le jour = vue Jour ; un seul niveau de détail*
- [ ] 🟢 Raccourcis Ctrl+Alt+1…5 entre les vues
- [ ] 🟢 **Échelle de temps** : 5, 6, 10, 15, 30, 60 minutes
- [ ] 🟢 **Heures de travail** : jours ouvrés, heures de début / fin, premier jour de la semaine (options)
- [ ] 🟡 **Second fuseau horaire** dans la grille
- [ ] 🟠 **Glisser-déposer** d'un événement vers un autre créneau / jour / calendrier (déplacer, copier, redimensionner la durée)

### Navigation

- [x] 🟢 **Barre de navigation** : *Aujourd'hui*, précédent / suivant, plage affichée (« 26 – 30 octobre 2026 ») <a id="demo-cal-navigation"></a> — *périodes Jour / Semaine / Mois, mémorisée*
- [ ] 🟢 **Atteindre** : Aujourd'hui, 7 prochains jours, aller à une date (Ctrl+G)
- [ ] 🟡 **Mini-calendrier** dans le volet des dossiers (jours avec événements en gras)
- [ ] 🟡 Météo (optionnelle)

### Plusieurs calendriers

- [x] 🟢 **Couleur par calendrier** (couleur EDS `ESourceSelectable`), dans la liste et dans la grille <a id="demo-cal-couleurs"></a> — *liste des événements et volet des calendriers ; grille quand elle existera (§12)*
- [ ] 🟠 **Superposé (vue globale)** : calendriers sélectionnés fusionnés dans une même grille
- [ ] 🟠 **Côte à côte** : une colonne par calendrier, même période, défilement commun
- [ ] 🟢 Bascule superposé / côte à côte ; mémorisation du mode et des calendriers affichés — *fait : calendriers affichés mémorisés, calendrier actif des nouveaux événements*
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
  - [ ] 🟠 Parcourir / rechercher les listes de salles Exchange (*room lists*)
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
- Calendriers de groupes Microsoft 365 : cf. « Groupes Microsoft 365 », §7

## 15. Contacts — 🟡

- [ ] 🟡 **Carnet d'adresses global** (GAL) Exchange / M365 : recherche et sélection des destinataires (backend evolution-ews) <a id="demo-gal"></a>
- Contacts unifiés et doublons : cf. §11 ; recherche rapide de contacts : cf. §10
- [ ] 🟡 **Fenêtre de sélection de destinataires** (carnet d'adresses, comme le bouton *À…* d'Outlook) :
  - [ ] Ouverte par un bouton en face de chaque ligne *À*, *Cc*, *Cci* de la fenêtre de rédaction, et en face de la ligne des participants (*Obligatoires*, *Facultatifs*, *Ressources*) d'une invitation (§13)
  - [ ] Fenêtre indépendante redimensionnable : choix du carnet d'adresses (tous les carnets, carnet d'un compte, annuaire GAL Exchange / M365, listes de salles), champ de recherche (nom, prénom, adresse, société), liste des résultats (nom, adresse, société, téléphone)
  - [ ] Boutons *À →*, *Cc →*, *Cci →* (ou *Obligatoires →*, *Facultatifs →*, *Ressources →* pour une invitation) et double-clic pour ajouter ; sélection multiple ; destinataires choisis affichés en bas de chaque ligne, supprimables
  - [ ] Listes de distribution / groupes de contacts : ajout du groupe entier ou développement en membres
  - [ ] *OK* : les destinataires sont ajoutés aux champs correspondants de la rédaction / de l'invitation ; *Annuler* : aucun changement
  - [ ] Fiche du contact accessible depuis la liste (clic droit › *Propriétés*), création d'un nouveau contact depuis la fenêtre
- [ ] 🟡 **Autocomplétion des adresses** dans les champs *À*, *Cc*, *Cci* de la rédaction et dans les participants d'une invitation : <a id="demo-autocompletion"></a>
  - [ ] Suggestions au fil de la frappe (à partir de 2 caractères) depuis tous les carnets d'adresses (requête préfixe `EBookClient` sur nom, prénom, adresse), puis l'annuaire GAL pour les comptes Exchange / M365
  - [ ] Adresses récemment utilisées en tête (destinataires des derniers messages envoyés), même si absentes des contacts
  - [ ] Liste déroulante : nom, adresse, carnet d'origine ; navigation aux flèches, validation par Entrée / Tab / point-virgule
  - [ ] Destinataire validé affiché en « jeton » (nom, adresse en info-bulle), supprimable, double-clic pour le modifier ; adresse inconnue ou mal formée soulignée
  - [ ] Plusieurs adresses collées d'un coup (séparées par `;` ou `,`) découpées en jetons
  - [ ] Suppression d'une suggestion récente (bouton ✕ dans la liste)
- [ ] 🟡 **Créer rapidement une fiche contact depuis un mail ou une invitation** :
  - [ ] Clic droit (ou survol) sur une adresse — expéditeur, destinataires, Cc, organisateur ou participant d'une invitation — : *Ajouter aux contacts*, *Modifier le contact* si l'adresse est déjà connue
  - [ ] Fiche préremplie : nom et adresse email tirés de l'en-tête ; 🟠 en option, téléphone, société, fonction et site web extraits de la signature du message
  - [ ] 🟡 Pour les comptes Exchange / M365 : compléter la fiche depuis l'annuaire (GAL) si la personne y figure
  - [ ] Choix du carnet d'adresses de destination (carnet par défaut, §3) ; détection des doublons (adresse déjà présente → proposer de compléter la fiche existante)
  - [ ] Ajout en un clic sans ouvrir la fiche (enregistrement direct) ou ouverture de la fiche pour compléter
  - [ ] Ajout groupé : tous les participants d'une invitation / tous les destinataires d'un mail
  - [ ] Indicateur visuel sur les adresses déjà présentes dans les contacts (carte de contact au survol : nom, société, téléphone, actions *Écrire*, *Planifier une réunion*)

## 16. Tâches — 🟠

Les listes de tâches EDS (`E_SOURCE_EXTENSION_TASK_LIST`, composants `VTODO`) ne sont pas encore exploitées par `evo-core` ; evolution-ews les synchronise déjà pour Exchange (et Microsoft To Do via Graph à prévoir).

### Module Tâches

- [x] 🟡 **Accès aux tâches dans `evo-core`** : lister, créer, modifier, supprimer (`E_CAL_CLIENT_SOURCE_TYPE_TASKS`)
- [x] 🟡 **Vue Tâches** (entrée *Tâches* de la barre latérale) : liste des tâches par liste / compte, avec colonnes objet, échéance, priorité, état, catégorie
- [x] 🟡 **Éditeur de tâche** : objet, description, date de début, échéance, priorité, état (non commencée, en cours, terminée, en attente, différée), pourcentage d'achèvement, catégories
- [ ] 🟠 Tâches périodiques
- [ ] 🟠 Rappel sur une tâche — dépend de la fenêtre des rappels (§2)
- [x] 🟢 **Marquer comme terminée** (case à cocher dans la liste), masquer / afficher les tâches terminées
- [x] 🟢 **Filtres et tri** : aujourd'hui, en retard, cette semaine, sans date, par priorité / échéance
- [x] 🟢 **Nouvelle tâche** depuis le menu *Nouveaux éléments* et raccourci (Ctrl+Maj+K)
- [ ] 🟡 Plusieurs listes de tâches (créer, renommer, supprimer)
- [ ] 🟠 Tâches Microsoft To Do (Graph) pour les comptes M365

### Lien entre mails, événements et tâches

- [ ] 🟡 **Action « Créer une tâche »** directement depuis un mail ou un événement du calendrier : <a id="demo-creer-tache"></a>
  - [ ] Accès : bouton du ruban et de la fenêtre de lecture, menu contextuel, raccourci clavier, glisser-déposer du mail / de l'événement sur la barre des tâches latérale ou sur *Tâches*
  - [ ] Depuis un mail : objet → titre, corps → description, message lié (`Message-ID`) pour le rouvrir depuis la tâche, catégories et indicateur Urgent / À traiter (§7) repris
  - [ ] Depuis un événement : titre, description et participants repris, échéance proposée = date de l'événement, lien vers l'événement
  - [ ] Création rapide (saisie de l'échéance et de la priorité dans un petit menu, sans ouvrir l'éditeur) ou ouverture de l'éditeur complet
  - [ ] Indicateur « tâche créée » sur le mail / l'événement d'origine
- [ ] 🟠 **Attribuer un mail à une tâche existante** : sélection de la tâche dans une liste, plusieurs mails possibles par tâche
  - [ ] Lien conservé par l'en-tête `Message-ID` (propriété `X-` / `ATTACH` dans le `VTODO`), ouverture du mail d'origine depuis la tâche
  - [ ] Indicateur sur le mail (icône « lié à une tâche ») et accès à la tâche depuis le volet de lecture
- Liste des tâches sous les vues Jour / Semaine du calendrier : cf. §12

### Barre latérale des tâches (à droite)

- [ ] 🟡 **Volet latéral droit** affichant une liste courte des tâches ouvertes, disponible dans les vues Mail, Calendrier et Contacts (équivalent de la To-Do bar d'Outlook) <a id="demo-volet-taches"></a>
  - [ ] 🟡 En haut du volet (optionnel) : mini-calendrier et prochains rendez-vous
  - [ ] Ordre : tâches en retard en haut, puis par **échéance la plus proche** ; à échéance égale, par **priorité décroissante** (haute → basse) ; tâches sans échéance en bas
  - [ ] Ligne compacte : case *Terminé*, titre, échéance (relative : « Aujourd'hui », « Demain », « Ven. 6 nov. », en rouge si dépassée), icône de priorité, pastille de catégorie, icône si liée à un mail / événement
  - [ ] Regroupement optionnel : En retard / Aujourd'hui / Demain / Cette semaine / Plus tard / Sans date
  - [ ] Champ « Nouvelle tâche » en haut (saisie rapide + Entrée)
  - [ ] Clic : ouvre la tâche ; double-clic sur l'icône de lien : ouvre le mail / l'événement d'origine
  - [ ] Glisser-déposer d'un mail / événement dans le volet pour créer une tâche
  - [ ] Volet affichable / masquable (menu *Affichage*, raccourci), largeur redimensionnable et mémorisée ; nombre de tâches affichées limité (ex. 15) avec lien *Voir toutes les tâches*
  - [ ] Mise à jour en direct (création, modification, synchronisation serveur)
- [ ] 🟢 Recherche dans les tâches — cf. §10

## 17. Catégories et codes couleur — 🟡

- [ ] 🟡 **Catégories communes** à tous les types d'éléments : nom + couleur (palette d'une vingtaine de couleurs), gérées dans les Réglages (créer, renommer, changer la couleur, supprimer) <a id="demo-categories"></a>
  - [ ] Stockage : étiquettes Camel pour les mails, propriété `CATEGORIES` pour les contacts (vCard), les événements et les tâches (iCalendar)
  - [ ] 🟠 Synchronisation avec la liste de catégories principale Exchange / M365 (couleurs comprises)
- [ ] 🟡 **Catégoriser** un élément (plusieurs catégories possibles) : menu contextuel, bouton *Catégoriser* du ruban, catégories récentes ; raccourci vers une catégorie (Ctrl+F2…F12)
- [ ] **Affichage des codes couleur** :
  - [ ] 🟡 **Mails** : pastilles de couleur dans la liste des messages et dans l'en-tête du volet de lecture
  - [ ] 🟡 **Contacts** : pastilles dans la liste et sur la fiche
  - [ ] 🟡 **Événements** : couleur de la catégorie dans la grille du calendrier (à la place ou en complément de la couleur du calendrier, au choix)
  - [ ] 🟡 **Tâches** : pastilles dans la liste des tâches
- [ ] 🟠 **Mise en forme conditionnelle** : couleur automatique selon des règles (expéditeur, domaine, mots de l'objet, importance, organisateur…), pour la liste des messages (§5) comme pour le calendrier (§12)
- [ ] 🟢 **Filtrer / trier / regrouper par catégorie** dans toutes les vues ; recherche par catégorie (§10)

## 18. Import / Export — 🟡

- [ ] 🟡 **Assistant d'export** (*Fichier › Exporter*) : compte, éléments (emails, contacts, calendriers, tâches), dossier de destination
- [ ] **Emails** :
  - [ ] 🟡 Format mbox (un fichier par dossier, arborescence conservée)
  - [ ] 🟢 Format EML (un fichier `.eml` par message, arborescence conservée ; export `.eml` existant dans `evo-core`)
  - [ ] 🟢 Choix des dossiers (tous / sélection / dossier courant)
- [x] 🟢 **Contacts** : vCard (`.vcf`, fichier unique ou un par contact) ; 🟡 CSV en option
- [x] 🟢 **Calendriers et tâches** : iCalendar (`.ics`, un fichier par calendrier)
- [ ] 🟡 **Options** : filtre par plage de dates, export en arrière-plan avec progression (dépend de §4), rapport de fin
- [ ] 🟠 **Import** : mbox / EML, vCard, iCalendar (*Fichier › Ouvrir et exporter*)

## 19. Configuration de l'application — 🟡

Page *Paramètres* regroupant les préférences générales (les réglages propres à un compte restent au §3). Préférences enregistrées dans un fichier de configuration utilisateur (`%LOCALAPPDATA%\EvolutionWin`), l'application n'étant pas empaquetée.

### Réorganisation de la page Paramètres

Aujourd'hui, une seule page à faire défiler (`SettingsViewPane`) enchaîne Comptes, Confidentialité, Signatures et Signature par compte, sans navigation ni regroupement logique ; elle deviendra illisible avec les réglages à venir.

- [ ] 🟡 **Navigation par catégories** sur le modèle des Paramètres de Windows 11 : liste des catégories à gauche (`NavigationView` en mode gauche ou liste), contenu de la catégorie à droite, fil d'Ariane en haut (« Paramètres › Comptes › Compte X ») <a id="demo-parametres"></a>
  - [ ] Catégories proposées : **Comptes**, **Courrier** (lecture, rédaction, signatures, contenu externe), **Calendrier**, **Contacts**, **Tâches**, **Notifications**, **Apparence** (thème, langue, disposition), **Confidentialité et sécurité**, **Mise à jour**, **Avancé** (caches, journaux, réinitialisation), **À propos**
- Page détaillée par compte (clic sur un compte dans la catégorie Comptes) : cf. « Modifier un compte existant », §3
- [ ] 🟢 **Contrôles homogènes** : cartes de réglage façon Windows 11 (`SettingsCard` / `SettingsExpander` du Windows Community Toolkit, ou équivalent maison) — titre, description courte, contrôle aligné à droite ; réglages avancés repliés dans un expandeur
- [ ] 🟢 **Recherche dans les Paramètres** : champ en haut de la page qui filtre les réglages par mots-clés et ouvre directement la bonne catégorie
- [ ] 🟢 **Enregistrement immédiat** des modifications (pas de bouton *Enregistrer* global) avec confirmation discrète ; boutons *Annuler* / *Enregistrer* seulement dans les éditeurs (signature, compte)
- [ ] 🟢 **Accès contextuel** : liens directs depuis l'interface vers le réglage concerné (ex. bandeau « contenu externe bloqué » → *Confidentialité*, icône d'état des connexions → page du compte, menu contextuel d'un compte → *Paramètres du compte*)
- [ ] 🟢 **Rétablir les valeurs par défaut** par catégorie
- [ ] 🟢 Ouverture des Paramètres par Ctrl+, et depuis *Fichier › Options*
- [ ] 🟢 Textes d'aide courts sous chaque réglage, sans jargon technique (laisser les détails de protocole dans la page *Avancé*)

### Notifications

- [ ] 🟢 **Activer / désactiver les notifications Windows** (toasts) de nouveau courrier, globalement et par compte / dossier
- [x] 🟢 **Mode confidentiel des notifications** : remplacer l'expéditeur, l'objet et l'extrait par un message générique (« Nouveau message », « 3 nouveaux messages ») — utile en présentation ou en partage d'écran <a id="demo-confidentiel"></a>
  - [x] Bascule rapide depuis la barre d'état / le menu de l'icône, sans passer par les Paramètres — *ruban Affichage, barre d'accès rapide et Paramètres › Confidentialité*
  - [ ] 🟡 Activation automatique quand Windows est en mode *Ne pas déranger* / partage d'écran détecté (optionnel)
  - [ ] S'applique aussi aux rappels de rendez-vous et de tâches (titre et lieu masqués) — *quand les rappels existeront (§2)*
- [ ] 🟢 Son de notification (aucun / son Windows / personnalisé), badge de non-lus sur l'icône de la barre des tâches
- [ ] 🟢 Notifications des rappels de calendrier et de tâches (activer / désactiver, durée d'affichage)

### Général

- [ ] 🟢 Langue de l'interface (cf. §20), thème (clair / sombre / système), taille du texte
- [ ] 🟢 Démarrage avec Windows, réduction dans la zone de notification à la fermeture
- [ ] 🟢 Module affiché au démarrage (Mail, Calendrier, Contacts, Tâches)
- [ ] 🟡 Raccourcis clavier (afficher la liste, profil Outlook / Evolution)

### Mail

- [ ] 🟢 Intervalle de relève automatique (5 minutes actuellement ; valeur par défaut, remplaçable par compte, §3), relève au démarrage
- [ ] 🟢 Marquer comme lu : immédiatement / après N secondes / jamais à l'affichage dans le volet de lecture
- [ ] 🟢 Format de rédaction par défaut (HTML / texte brut), police et taille par défaut, correcteur orthographique (langue)
- [ ] 🟢 Confirmations : avant suppression définitive, avant d'envoyer sans objet, pièce jointe mentionnée mais absente
- [ ] 🟢 Vider la Corbeille à la fermeture
- [ ] 🟢 Contenu externe : cf. §6 ; signatures : cf. §3

### Calendrier

- [ ] 🟢 Heures et jours de travail, premier jour de la semaine, fuseau horaire, numéros de semaine — cf. §12
- [ ] 🟢 Rappel par défaut des nouveaux événements, durée par défaut des réunions

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
