# TODO

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
- [x] 🟢 **Menu « À propos »** (menu *Aide* / *Fichier*, ou entrée en bas de la barre latérale) : boîte de dialogue avec icône, nom, version en cours, licence (LGPL 2.1 ou ultérieure), lien vers le dépôt GitHub et le [CHANGELOG.md](CHANGELOG.md)
  - [x] Version tirée du fichier [VERSION](VERSION) à la compilation (macro / ressource `VERSIONINFO` générée par MSBuild, comme le fait déjà `build-msi.ps1` pour le MSI), pour que l'exécutable, le MSI et la boîte « À propos » affichent toujours la même version
  - [x] Versions des composants (EDS, evolution-ews, Windows App SDK, WebView2) et bouton « Copier les informations » pour les rapports de bug

## 2. Interface générale et disposition — 🟡

### Structure de la fenêtre

- [ ] 🟡 **Barre d'outils à onglets façon ruban** pour les vues Mail et Calendrier : *Fichier*, *Accueil*, *Envoi/réception*, *Dossier*, *Affichage* (WinUI n'a pas de ruban : `TabView` / `SelectorBar` + `CommandBar`). Hors périmètre : intégrations tierces (Teams, OneNote, TeamViewer, Rainbow, Copilot)
- [ ] 🟡 **Barre d'accès rapide** personnalisable dans la barre de titre (Envoyer/Recevoir, Annuler, etc.)
- [ ] 🟢 **Barre latérale gauche** de navigation entre modules (Mail, Calendrier, Contacts, Tâches) — `NavigationView` déjà en place, à restyler en barre d'icônes
- [ ] 🟢 **Bandeau d'information** sous la barre d'outils (`InfoBar`), affiché dans les vues Mail et Calendrier (ex. réponses automatiques actives + bouton *Désactiver*)

### Disposition des panneaux

- [ ] 🟢 **Panneaux redimensionnables** : séparateurs déplaçables entre dossiers, liste des messages et volet de lecture ; tailles mémorisées entre deux sessions
- [ ] 🟡 **Position du volet de lecture** (menu *Affichage › Volet de lecture*), en Mail comme en Calendrier :
  - [ ] 🟢 À droite
  - [ ] 🟢 En bas
  - [ ] 🟡 Désactivé (ouverture des messages / événements dans une fenêtre séparée)
  - [ ] 🟢 Choix mémorisé dans les préférences
- [ ] 🟢 **Volet des dossiers** : normal / réduit / désactivé
- [ ] 🟢 **Espacement plus étroit** (mode compact) des listes
- [ ] 🟠 **Barre des tâches (To-Do bar)** à droite : mini-calendrier, rendez-vous à venir, personnes, tâches

### Fenêtres et lecture

- [ ] 🟡 **Ouvrir dans une nouvelle fenêtre** (message, événement, dossier) ; 🟢 **Fermer tous les éléments ouverts**
- [ ] 🟠 **Fenêtre des rappels** : rappels des rendez-vous (alarmes EDS) et des messages en suivi, avec Reporter / Ignorer
- [ ] 🟡 **Lecteur immersif** : vue de lecture plein écran épurée (taille du texte, espacement)
- [ ] 🟢 **Lecture à voix haute** du message (`Windows.Media.SpeechSynthesis`)

## 3. Comptes — 🟠

### Ajout unifié avec détection automatique

- [ ] 🟢 **Bouton générique « Ajouter un compte »** à la place de « Ajouter un compte IMAP/SMTP » (menu Réglages)
- [ ] 🟠 **Détection automatique du type de compte** à partir de l'adresse email / du domaine :
  - [ ] Microsoft 365 / Exchange Online (OAuth2 + Graph / EWS)
  - [ ] Exchange on-premise (Autodiscover → EWS)
  - [ ] Gmail / Google Workspace (OAuth2, IMAP/SMTP Google)
  - [ ] IMAP/SMTP générique (repli par défaut)
- [ ] **Mécanismes de détection** (dans l'ordre) :
  - [ ] 🟢 Fournisseurs connus (gmail.com, outlook.com, etc.)
  - [ ] 🟡 Enregistrements MX du domaine (`*.mail.protection.outlook.com`, `*.google.com`), via `DnsQuery`
  - [ ] 🟡 Autodiscover Exchange / Microsoft 365
  - [ ] 🟡 Autoconfig Thunderbird (`autoconfig.<domaine>`, base ISPDB)
  - [ ] 🟢 Enregistrements DNS SRV (`_imaps._tcp`, `_submission._tcp`)
- [ ] 🟡 **Confirmation / correction manuelle** : type détecté et paramètres proposés, possibilité de forcer un autre type ou de passer en configuration manuelle

### Compte par défaut

- [ ] 🟢 **Désigner le compte par défaut** dans la liste des comptes, avec indication visuelle (`e_source_registry_set_default_mail_account`)
- [ ] 🟢 Utilisé pour les nouveaux messages ; pour une réponse / un transfert, compte ayant reçu le message (repli sur le compte par défaut)
- [ ] 🟢 Calendrier et carnet d'adresses par défaut pour les nouveaux événements et contacts
- [ ] 🟡 Gestionnaire des liens `mailto:` (inscription d'EvolutionWin comme application de messagerie Windows)

### Signatures — 🟡

- [ ] 🟡 **Éditeur de signatures** (texte et HTML : mise en forme, images, liens) dans les Réglages — réutilise l'éditeur WebView2 de la rédaction ; sources EDS `ESourceMailSignature`
- [ ] 🟢 Créer / modifier / supprimer plusieurs signatures nommées
- [ ] 🟢 Par compte : signature des nouveaux messages et signature des réponses / transferts (ou aucune)
- [ ] 🟡 Changement de signature dans la fenêtre de rédaction, mise à jour automatique au changement de compte expéditeur
- [ ] 🟢 Placement (au-dessus / en dessous du texte cité)

### Réponses automatiques (Out of Office) — 🟠

- [ ] **Gestion des réponses automatiques** (*Fichier › Réponses automatiques* et menu contextuel du compte) :
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

- [ ] 🟡 Paramètres du compte, quota (`camel_folder_get_quota_info_sync`), outils de nettoyage de la boîte aux lettres
- [ ] 🟡 Options générales de l'application — cf. §19

## 4. Synchronisation et mode hors connexion — 🟠

### Progression

- [ ] 🟡 **Barre de progression dans la barre d'état** lors d'un rafraîchissement (messages de progression `CamelOperation` relayés par `evo-core`) :
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
- [ ] 🟢 **Indicateur du mode hors connexion dans la barre d'état** (en bas) : icône et message « Hors connexion — N messages en attente d'envoi », état par compte (serveur injoignable, authentification requise), bouton *Réessayer maintenant* / *Travailler en ligne*
- [ ] 🟡 **Icône d'état des connexions** dans la barre d'état (verte : tout est connecté ; orange : connexion en cours / partielle ; rouge : erreur ; grise : hors connexion)
  - [ ] Clic : panneau listant chaque compte et chacun de ses services (IMAP, SMTP, Exchange / EWS, Microsoft Graph, Gmail, CalDAV / CardDAV…) avec état, serveur, dernière synchronisation réussie
  - [ ] Message d'erreur explicite en cas d'échec : serveur injoignable, certificat refusé, délai dépassé, quota, **authentification requise**
  - [ ] **Réauthentification** mise en évidence (mot de passe changé, jeton OAuth2 expiré ou révoqué, changement de compte / de tenant, MFA) : bouton *Se reconnecter* qui relance la saisie du mot de passe ou la connexion OAuth2 sans passer par les Paramètres
  - [ ] Bouton *Réessayer* par compte ; lien vers les paramètres du compte
  - [ ] Pastille d'erreur sur le compte concerné dans l'arborescence des dossiers et notification Windows (une seule fois par erreur) quand une réauthentification est nécessaire
  - [ ] État fourni par `evo-core` (événement `connection-status` par service, à partir de `CamelService:connection-status`, des erreurs de relève et de l'état des sources EDS)
- [ ] 🟡 **Préférences de téléchargement** : en-têtes seuls / message complet, limite de taille, synchronisation hors connexion des dossiers
- [ ] 🟠 **Mode en-têtes seuls** : télécharger les en-têtes, marquer pour téléchargement (message / copie), annuler les marques, traiter les en-têtes marqués

## 5. Mail – Liste des messages — 🟠

### Tri et colonnes

- [ ] 🟡 **Trier les emails** par clic sur l'en-tête de colonne et via *Affichage › Trier par* (la `ListView` actuelle n'a pas d'en-têtes de colonnes) :
  - [ ] Date, Expéditeur, Destinataire, Objet, Taille, Pièces jointes, Importance, Indicateur (état / date de début / d'échéance), Catégories, Lu / non lu, Compte, Type
  - [ ] 🟢 Inverser l'ordre du tri pour chaque critère
  - [ ] 🟢 Mémoriser le tri par dossier
- [ ] 🟡 **Regroupement** par critère de tri (Aujourd'hui, Hier, La semaine dernière…), avec Développer / Réduire les groupes
- [ ] 🟡 **Ajouter / retirer des colonnes**
- [ ] 🟡 **Changer l'affichage** : Compact, Unique, Aperçu ; 🟢 Rétablir l'affichage par défaut
- [ ] 🟠 **Paramètres d'affichage avancés** : polices, mise en forme conditionnelle (couleur selon expéditeur, catégorie…)
- [ ] 🟡 **Aperçu du message** : désactivé, 1, 2 ou 3 lignes sous l'objet (extrait du corps à stocker dans le résumé)

### Barre de lecture rapide

- [ ] 🟢 **Barre verticale à gauche de chaque message** (comme Outlook) : affichée en couleur d'accent pour les messages non lus, invisible pour les lus
  - [ ] Clic sur la barre : bascule Lu / Non lu sans ouvrir ni sélectionner le message (fonction `evo-core` existante)
  - [ ] Survol : barre mise en évidence et info-bulle « Marquer comme lu » / « Marquer comme non lu »
  - [ ] Fonctionne aussi sur un en-tête de conversation (marque tout le fil) et dans les vues unifiées

### Filtres rapides

- [ ] 🟢 **Filtres rapides** au-dessus de la liste (boutons bascule, comme dans Outlook) :
  - [ ] Tous / Non lus / Lus
  - [ ] Avec indicateur, avec pièces jointes, cette semaine, importants, mentionné
  - [ ] Combinables avec la recherche et fonctionnels dans les vues unifiées

### Conversations

- [ ] 🟠 **Grouper par discussion** (*Affichage › Afficher sous forme de conversations*) :
  - [ ] Regroupement par `Message-ID` / `In-Reply-To` / `References` (`CamelFolderThread`), repli sur l'objet normalisé « RE: / TR: »
  - [ ] Fil réductible / développable, nombre de messages et de non lus
  - [ ] 🟠 Inclure les messages d'autres dossiers (réponses envoyées)
  - [ ] 🟢 Choix mémorisé par dossier
- [ ] 🟡 **Paramètres de conversation** : expéditeurs au-dessus de l'objet, toujours développer la conversation sélectionnée, retrait classique

### Boîte de réception Prioritaire

- [ ] 🟠 **Onglets *Prioritaire* / *Autres*** (classement Exchange Online `inferenceClassification` via Graph ; sans objet pour les autres comptes)

## 6. Mail – Lecture des messages — 🟢

Le contenu distant est déjà bloqué par le lecteur WebView2 (`WebResourceRequested` → 403).

- [ ] 🟢 **Bandeau « contenu externe bloqué »** au-dessus du message :
  - [ ] « Afficher les images pour ce message » (une seule fois)
  - [ ] « Toujours faire confiance à l'expéditeur »
  - [ ] « Toujours faire confiance au domaine » (ex. `@exemple.ch`)
- [ ] 🟢 **Liste des expéditeurs / domaines de confiance** gérée dans les préférences
- [ ] 🟢 **Option globale** : jamais / expéditeurs de confiance seulement / toujours charger le contenu externe
- [ ] 🟢 **Enregistrer sous** (`.eml`, fonction `evo-core` existante), **Enregistrer toutes les pièces jointes**
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
- [ ] 🟡 **Double-clic sur un mail : ouverture dans une fenêtre indépendante** (aussi avec Entrée et *Ouvrir* du menu contextuel)
  - [ ] Fenêtre de lecture avec en-têtes, pièces jointes, bandeau de sécurité / contenu externe et barre d'actions (Répondre, Répondre à tous, Transférer, Supprimer, Déplacer, Lu / Non lu, Imprimer)
  - [ ] Navigation message précédent / suivant dans la fenêtre
  - [ ] Plusieurs fenêtres ouvertes simultanément ; taille et position mémorisées
  - [ ] Brouillon (dossier Brouillons) : double-clic ouvre la fenêtre de rédaction pour reprendre le message
  - [ ] Message marqué comme lu à l'ouverture

## 7. Mail – Actions sur les messages — 🟠

### Créer et répondre

- [ ] 🟢 **Nouveaux éléments** (menu) : courrier, rendez-vous, réunion, contact, tâche
- [ ] 🟡 **Répondre par une réunion** (réunion pré-remplie avec les participants) — dépend de §13
- [ ] 🟢 **Transférer en pièce jointe** pour un message seul (déjà fait en sélection multiple)

### Supprimer, archiver, déplacer

- [ ] 🟢 **Archiver** : déplacer vers le dossier d'archive du compte (réglage `archive-folder` d'EDS), touche Retour arrière
- [ ] 🟢 **Copier vers un dossier** ; liste des derniers dossiers utilisés dans *Déplacer*
- [ ] 🟠 **Ignorer la conversation** : les nouveaux messages du fil vont directement à la Corbeille
- [ ] 🟠 **Nettoyer la conversation** : supprimer les messages redondants (entièrement cités dans un message plus récent)

### Indicateurs et catégories

- [ ] 🟡 **Marquer un mail comme « Urgent » ou « À traiter »** (en complément du drapeau simple existant) :
  - [ ] Choix de l'état depuis le menu contextuel, le ruban et des raccourcis clavier ; plusieurs libellés prédéfinis (Urgent, À traiter, À répondre, Pour information, En attente) et libellés personnalisés
  - [ ] Icône et couleur distinctes dans la liste des messages (ex. rouge pour Urgent, orange pour À traiter) et libellé dans l'en-tête du volet de lecture
  - [ ] Stockage dans les indicateurs Camel (`follow-up` + étiquette dédiée), synchronisé avec le serveur (mots-clés IMAP, `flag` / importance Exchange)
  - [ ] Filtres rapides « Urgents » et « À traiter » (§5) et dossiers de recherche correspondants (§9)
  - [ ] Passage à « Terminé » en un clic, avec date de fin
- [ ] 🟡 **Catégoriser** le message (codes couleur) — cf. §17
- [ ] 🟡 **Créer une tâche à partir du message** — cf. §16
- [ ] 🟡 **Assurer un suivi** : aujourd'hui, demain, cette semaine, semaine prochaine, sans date, personnalisé, marquer comme terminé, effacer (le drapeau simple existe déjà)
  - [ ] 🟠 Ajouter un rappel — dépend de la fenêtre des rappels (§2)
- [ ] 🟡 **Importance** (haute / basse) affichée et modifiable

### Actions rapides

- [ ] 🟠 **Actions rapides** en un clic, personnalisables (déplacer + marquer comme lu, transférer à un responsable, message d'équipe…), avec éditeur d'actions

### Groupes Microsoft 365

- [ ] 🟠 **Nouveau groupe, Parcourir les groupes** (Graph, comptes M365)

## 8. Mail – Règles et courrier indésirable — 🔴

- [ ] 🔴 **Règles côté client** : éditeur de règles (conditions / actions), exécution à la réception (`CamelFilterDriver`)
  - [ ] 🟡 Créer une règle à partir du message sélectionné (une fois l'éditeur en place)
  - [ ] 🟡 Exécuter les règles maintenant sur un dossier
- [ ] 🔴 **Règles côté serveur** : Exchange (EWS / Graph `messageRules`), Sieve (ManageSieve)
- [ ] 🟡 **Courrier indésirable** : marquer comme indésirable / non indésirable (déplacement vers / depuis le dossier Indésirables)
- [ ] 🟡 **Listes d'expéditeurs** bloqués / approuvés (expéditeur ou domaine), options du filtre ; 🟠 synchronisées avec Exchange
- [ ] 🟡 **Signaler** un message comme hameçonnage / spam (transfert en pièce jointe vers une adresse configurable, ou API du fournisseur)

## 9. Mail – Gestion des dossiers — 🟠

- [ ] 🟡 **Nouveau dossier, Renommer, Copier, Déplacer, Supprimer** (`camel_store_create_folder_sync`, etc.), glisser-déposer dans l'arborescence
- [ ] 🟢 **Marquer tout comme lu**
- [ ] 🟢 **Supprimer tout** : vider le dossier, la Corbeille, les Indésirables
- [ ] 🟢 **Afficher tous les dossiers de A à Z**
- [ ] 🟠 **Nettoyer le dossier** (et sous-dossiers) des messages redondants — même logique que §7
- [ ] **Récupérer les éléments supprimés** :
  - [ ] 🟡 IMAP : messages marqués supprimés non purgés
  - [ ] 🟠 Exchange : *Recoverable Items*
- [ ] 🟡 **Favoris** : section *Favoris* en haut du volet des dossiers, réordonnable
- [ ] 🟢 **Afficher dans le navigateur** (OWA / webmail du fournisseur)
- [ ] 🟠 **Dossiers de recherche** (dossiers virtuels `CamelVeeFolder`, aujourd'hui ignorés par `evo-core`), avec modèles prédéfinis : non lus, marqués, de personnes spécifiques, gros messages…
- [ ] **Propriétés du dossier** :
  - [ ] 🟡 Taille, nombre d'éléments, afficher le total ou les non-lus, quota du compte
  - [ ] 🟠 Autorisations du dossier (partage / délégation Exchange, ACL IMAP)
  - [ ] 🟠 Paramètres d'archivage automatique (âge, déplacement vers un dossier / fichier d'archive, suppression)
  - [ ] 🟠 Stratégie de rétention (Exchange)

## 10. Recherche — 🟡

La recherche dans le dossier courant existe déjà (filtrage instantané + recherche en-têtes et corps via `evo-core`).

### Recherche simple

- [ ] 🟢 **Champ de recherche dans la barre de titre** de la fenêtre (au lieu de la vue Mail seule)
- [ ] 🟢 Bouton pour effacer la recherche ; raccourci Ctrl+E
- [ ] 🟡 **Portée** : dossier courant / sous-dossiers / compte courant / tous les comptes
- [ ] 🟢 **Recherche rapide de contacts** (champ dédié dans le ruban, comme Outlook) et accès au Carnet d'adresses
- [ ] 🟡 **Recherche dans le calendrier** (titre, lieu, description, participants)

### Recherche avancée

- [ ] 🟡 Accessible depuis le champ de recherche simple ; critères combinables : expéditeur, destinataire, objet, corps, plage de dates, taille, pièces jointes, lu / non lu, indicateur, catégorie
- [ ] 🟡 Opérateurs ET / OU entre les critères (expressions de recherche Camel)
- [ ] 🟠 Enregistrer une recherche comme dossier de recherche — dépend de §9

## 11. Vues unifiées multi-comptes — 🟠

- [ ] 🟠 **Mails** : Boîte de réception unifiée (et Envoyés, Brouillons, Corbeille), compte d'origine indiqué (colonne ou pastille de couleur) — dossiers virtuels Camel
- [ ] 🟡 **Calendriers** : événements de tous les calendriers de tous les comptes, une couleur par calendrier, chacun masquable
- [ ] 🟡 **Contacts** : carnet d'adresses unifié, détection et regroupement des doublons (même email)
- [ ] 🟠 **Recherche dans les vues unifiées** (simple et avancée) pour mails, événements et contacts, compte d'origine visible dans les résultats
- [ ] 🟢 **Choix des comptes inclus** dans les vues unifiées (préférences)

## 12. Calendrier – Vues et navigation — 🔴

La vue actuelle se limite à un `CalendarView` mensuel et une liste d'événements.

### Grille calendrier (brique préalable)

- [ ] 🔴 **Contrôle grille Jour / Semaine de travail / Semaine** (WinUI n'en fournit pas) : créneaux horaires, événements positionnés et chevauchements, journée entière en haut, défilement vertical
- [ ] 🟠 **Vue Mois** (détails faibles / moyens / élevés)
- [ ] 🟢 Raccourcis Ctrl+Alt+1…5 entre les vues
- [ ] 🟢 **Échelle de temps** : 5, 6, 10, 15, 30, 60 minutes
- [ ] 🟢 **Heures de travail** : jours ouvrés, heures de début / fin, premier jour de la semaine (options)
- [ ] 🟡 **Second fuseau horaire** dans la grille
- [ ] 🟠 **Glisser-déposer** d'un événement vers un autre créneau / jour / calendrier (déplacer, copier, redimensionner la durée)

### Navigation

- [ ] 🟢 **Barre de navigation** : *Aujourd'hui*, précédent / suivant, plage affichée (« 26 – 30 octobre 2026 »)
- [ ] 🟢 **Atteindre** : Aujourd'hui, 7 prochains jours, aller à une date (Ctrl+G)
- [ ] 🟡 **Mini-calendrier** dans le volet des dossiers (jours avec événements en gras)
- [ ] 🟡 Météo (optionnelle)

### Plusieurs calendriers

- [ ] 🟢 **Couleur par calendrier** (couleur EDS `ESourceSelectable`), dans la liste et dans la grille
- [ ] 🟠 **Superposé (vue globale)** : calendriers sélectionnés fusionnés dans une même grille
- [ ] 🟠 **Côte à côte** : une colonne par calendrier, même période, défilement commun
- [ ] 🟢 Bascule superposé / côte à côte ; mémorisation du mode et des calendriers affichés
- [ ] 🟡 **Groupes de calendriers** nommés (« Équipe », « Salles 2e étage ») pour en ouvrir plusieurs d'un coup

### Autres affichages

- [ ] 🟡 **Vues Liste / Actif** : liste filtrable des événements (à venir, périodiques…)
- [ ] 🟠 **Paramètres d'affichage** : champs, tri, filtre, mise en forme conditionnelle (catégorie / organisateur) ; 🟢 Rétablir l'affichage
- [ ] 🟡 **Volet de lecture** de l'événement sélectionné
- [ ] 🟠 **Liste des tâches quotidiennes** sous les vues Jour / Semaine (tâches EDS et messages en suivi par date d'échéance — dépend de §16)
- [ ] 🟢 Espacement plus étroit ; volet des dossiers normal / réduit (avec mini-calendrier) / désactivé

## 13. Calendrier – Rendez-vous, réunions et ressources — 🔴

- [ ] 🟠 **Éditeur d'événement complet** (brique préalable) : journée entière, périodicité (règles RRULE, exceptions), rappels, disponibilité (libre / occupé / provisoire / absent), catégories, privé, pièces jointes, description HTML
- [ ] 🟢 **Ajouter du temps de travail individuel** (bloc « focus » marqué occupé, sans participants)
- [ ] 🔴 **Nouvelle réunion** : participants (obligatoires / facultatifs), envoi des invitations, suivi des réponses (accepté / provisoire / refusé), mise à jour et annulation (iTIP / iMIP ; géré par le serveur pour Exchange)
- [ ] 🟡 **Répondre aux invitations reçues** depuis le volet de lecture du Mail (Accepter / Provisoire / Refuser / Proposer un nouvel horaire)
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
- [ ] 🟠 **Calendriers de groupes Microsoft 365** (Nouveau groupe, Parcourir les groupes)

## 15. Contacts — 🟡

- [ ] 🟡 **Carnet d'adresses global** (GAL) Exchange / M365 : recherche et sélection des destinataires (backend evolution-ews)
- [ ] 🟡 **Contacts unifiés** et regroupement des doublons — cf. §11
- [ ] 🟢 **Recherche rapide de contacts** depuis la barre d'outils — cf. §10

## 16. Tâches — 🟠

Les listes de tâches EDS (`E_SOURCE_EXTENSION_TASK_LIST`, composants `VTODO`) ne sont pas encore exploitées par `evo-core` ; evolution-ews les synchronise déjà pour Exchange (et Microsoft To Do via Graph à prévoir).

### Module Tâches

- [ ] 🟡 **Accès aux tâches dans `evo-core`** : lister, créer, modifier, supprimer (`E_CAL_CLIENT_SOURCE_TYPE_TASKS`)
- [ ] 🟡 **Vue Tâches** (entrée *Tâches* de la barre latérale) : liste des tâches par liste / compte, avec colonnes objet, échéance, priorité, état, catégorie
- [ ] 🟡 **Éditeur de tâche** : objet, description, date de début, échéance, priorité, état (non commencée, en cours, terminée, en attente, différée), pourcentage d'achèvement, catégories
- [ ] 🟠 Tâches périodiques
- [ ] 🟠 Rappel sur une tâche — dépend de la fenêtre des rappels (§2)
- [ ] 🟢 **Marquer comme terminée** (case à cocher dans la liste), masquer / afficher les tâches terminées
- [ ] 🟢 **Filtres et tri** : aujourd'hui, en retard, cette semaine, sans date, par priorité / échéance
- [ ] 🟢 **Nouvelle tâche** depuis le menu *Nouveaux éléments* et raccourci (Ctrl+Maj+K)
- [ ] 🟡 Plusieurs listes de tâches (créer, renommer, supprimer)
- [ ] 🟠 Tâches Microsoft To Do (Graph) pour les comptes M365

### Lien entre mails et tâches

- [ ] 🟡 **Créer une tâche à partir d'un mail** (menu contextuel, bouton du ruban, glisser-déposer du message sur *Tâches*) : objet et corps repris, message joint ou lié
- [ ] 🟠 **Attribuer un mail à une tâche existante** : sélection de la tâche dans une liste, plusieurs mails possibles par tâche
  - [ ] Lien conservé par l'en-tête `Message-ID` (propriété `X-` / `ATTACH` dans le `VTODO`), ouverture du mail d'origine depuis la tâche
  - [ ] Indicateur sur le mail (icône « lié à une tâche ») et accès à la tâche depuis le volet de lecture
- [ ] 🟡 **Liste des tâches dans la barre des tâches (To-Do bar)** et sous les vues Jour / Semaine du calendrier — cf. §2 et §12
- [ ] 🟢 Recherche dans les tâches — cf. §10

## 17. Catégories et codes couleur — 🟡

- [ ] 🟡 **Catégories communes** à tous les types d'éléments : nom + couleur (palette d'une vingtaine de couleurs), gérées dans les Réglages (créer, renommer, changer la couleur, supprimer)
  - [ ] Stockage : étiquettes Camel pour les mails, propriété `CATEGORIES` pour les contacts (vCard), les événements et les tâches (iCalendar)
  - [ ] 🟠 Synchronisation avec la liste de catégories principale Exchange / M365 (couleurs comprises)
- [ ] 🟡 **Catégoriser** un élément (plusieurs catégories possibles) : menu contextuel, bouton *Catégoriser* du ruban, catégories récentes ; raccourci vers une catégorie (Ctrl+F2…F12)
- [ ] **Affichage des codes couleur** :
  - [ ] 🟡 **Mails** : pastilles de couleur dans la liste des messages et dans l'en-tête du volet de lecture
  - [ ] 🟡 **Contacts** : pastilles dans la liste et sur la fiche
  - [ ] 🟡 **Événements** : couleur de la catégorie dans la grille du calendrier (à la place ou en complément de la couleur du calendrier, au choix)
  - [ ] 🟡 **Tâches** : pastilles dans la liste des tâches
- [ ] 🟠 **Mise en forme conditionnelle** : couleur automatique selon des règles (expéditeur, domaine, mots de l'objet, importance…) — cf. §5 et §12
- [ ] 🟢 **Filtrer / trier / regrouper par catégorie** dans toutes les vues ; recherche par catégorie (§10)

## 18. Import / Export — 🟡

- [ ] 🟡 **Assistant d'export** (*Fichier › Exporter*) : compte, éléments (emails, contacts, calendriers, tâches), dossier de destination
- [ ] **Emails** :
  - [ ] 🟡 Format mbox (un fichier par dossier, arborescence conservée)
  - [ ] 🟢 Format EML (un fichier `.eml` par message, arborescence conservée ; export `.eml` existant dans `evo-core`)
  - [ ] 🟢 Choix des dossiers (tous / sélection / dossier courant)
- [ ] 🟢 **Contacts** : vCard (`.vcf`, fichier unique ou un par contact) ; 🟡 CSV en option
- [ ] 🟢 **Calendriers et tâches** : iCalendar (`.ics`, un fichier par calendrier)
- [ ] 🟡 **Options** : filtre par plage de dates, export en arrière-plan avec progression (dépend de §4), rapport de fin
- [ ] 🟠 **Import** : mbox / EML, vCard, iCalendar (*Fichier › Ouvrir et exporter*)

## 19. Configuration de l'application — 🟡

Page *Paramètres* regroupant les préférences générales (les réglages propres à un compte restent au §3). Préférences enregistrées dans un fichier de configuration utilisateur (`%LOCALAPPDATA%\EvolutionWin`), l'application n'étant pas empaquetée.

### Notifications

- [ ] 🟢 **Activer / désactiver les notifications Windows** (toasts) de nouveau courrier, globalement et par compte / dossier
- [ ] 🟢 **Mode confidentiel des notifications** : remplacer l'expéditeur, l'objet et l'extrait par un message générique (« Nouveau message », « 3 nouveaux messages ») — utile en présentation ou en partage d'écran
  - [ ] Bascule rapide depuis la barre d'état / le menu de l'icône, sans passer par les Paramètres
  - [ ] 🟡 Activation automatique quand Windows est en mode *Ne pas déranger* / partage d'écran détecté (optionnel)
  - [ ] S'applique aussi aux rappels de rendez-vous et de tâches (titre et lieu masqués)
- [ ] 🟢 Son de notification (aucun / son Windows / personnalisé), badge de non-lus sur l'icône de la barre des tâches
- [ ] 🟢 Notifications des rappels de calendrier et de tâches (activer / désactiver, durée d'affichage)

### Général

- [ ] 🟢 Langue de l'interface, thème (clair / sombre / système), taille du texte
- [ ] 🟢 Démarrage avec Windows, réduction dans la zone de notification à la fermeture
- [ ] 🟢 Module affiché au démarrage (Mail, Calendrier, Contacts, Tâches)
- [ ] 🟡 Raccourcis clavier (afficher la liste, profil Outlook / Evolution)

### Mail

- [ ] 🟢 Intervalle de relève automatique (5 minutes actuellement), relève au démarrage
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
