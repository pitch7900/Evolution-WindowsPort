# TODO

## Interface – Disposition des panneaux

- [ ] **Panneaux redimensionnables** : permettre de redimensionner à la souris les séparateurs entre les panneaux (liste des dossiers, liste des messages, volet de lecture), et mémoriser les tailles entre deux sessions.
- [ ] **Position du volet de lecture (comme dans Outlook)** : ajouter une option (menu *Affichage › Volet de lecture*) pour choisir l'emplacement de l'aperçu des emails :
  - [ ] À droite (liste des messages à gauche, aperçu à droite)
  - [ ] En bas (liste des messages en haut, aperçu en dessous)
  - [ ] Désactivé (pas de volet de lecture, ouverture des messages en fenêtre séparée)
  - [ ] Mémoriser le choix dans les préférences utilisateur

## Liste des messages – Tri

- [ ] **Trier les emails** : permettre de trier la liste des messages via un clic sur l'en-tête de colonne et via un menu *Affichage › Trier par* :
  - [ ] Date (croissante / décroissante)
  - [ ] Expéditeur
  - [ ] Destinataire
  - [ ] Objet
  - [ ] Taille
  - [ ] Pièces jointes
  - [ ] Importance / indicateur
  - [ ] Lu / non lu
  - [ ] Inverser l'ordre du tri (ascendant / descendant) pour chaque critère
  - [ ] Mémoriser le tri par dossier

## Liste des messages – Filtres rapides et discussions

- [ ] **Filtres rapides** au-dessus de la liste des messages (boutons bascule, comme « Tous / Non lus » dans Outlook) :
  - [ ] Tous / Non lus / Lus
  - [ ] Avec indicateur, avec pièces jointes (optionnel)
  - [ ] Combinables avec la recherche et fonctionnels dans les vues unifiées
- [ ] **Grouper par discussion** : option (menu *Affichage › Afficher sous forme de conversations*) pour regrouper les messages d'un même fil :
  - [ ] Regroupement basé sur les en-têtes `Message-ID` / `In-Reply-To` / `References` (repli sur l'objet normalisé « RE: / TR: »)
  - [ ] Fil réductible / développable, avec nombre de messages et de non lus
  - [ ] Inclure les réponses envoyées (dossier Envoyés) dans la discussion
  - [ ] Mémoriser le choix par dossier

## Recherche

- [ ] **Recherche simple dans la barre principale** : ajouter un champ de recherche dans la barre d'outils principale de la fenêtre :
  - [ ] Recherche plein texte (expéditeur, destinataires, objet, corps) dans le dossier courant
  - [ ] Filtrage de la liste des messages au fil de la frappe (ou validation par Entrée)
  - [ ] Choix de la portée : dossier courant / sous-dossiers / compte courant / tous les comptes
  - [ ] Bouton pour effacer la recherche et revenir à la liste complète
  - [ ] Raccourci clavier pour activer le champ (Ctrl+E, comme dans Outlook)
- [ ] **Recherche avancée** (accessible depuis le champ de recherche simple) :
  - [ ] Critères combinables : expéditeur, destinataire, objet, corps, plage de dates, taille, pièces jointes, lu / non lu, indicateur
  - [ ] Opérateurs ET / OU entre les critères
  - [ ] Enregistrer une recherche comme dossier de recherche (vFolder)

## Affichage des messages – Contenu externe

- [ ] **Afficher les images / contenus externes** : par défaut, bloquer le chargement des ressources distantes (images, pixels de suivi) et afficher un bandeau au-dessus du message :
  - [ ] « Afficher les images pour ce message » (une seule fois)
  - [ ] « Toujours faire confiance à l'expéditeur » (adresse email)
  - [ ] « Toujours faire confiance au domaine » (ex. `@exemple.ch`)
- [ ] **Liste des expéditeurs / domaines de confiance** : gérer la liste dans les préférences (ajout, suppression)
- [ ] **Option globale** : jamais / seulement pour les expéditeurs de confiance / toujours charger le contenu externe

## Comptes – Ajout unifié avec détection automatique

- [ ] **Remplacer « Ajouter un compte IMAP/SMTP »** par un bouton générique « Ajouter un compte » dans la liste des comptes du menu Réglages
- [ ] **Détection automatique du type de compte** à partir de l'adresse email / du domaine saisi :
  - [ ] Microsoft 365 / Exchange Online (domaine hébergé chez Microsoft, OAuth2 + Graph / EWS)
  - [ ] Exchange on-premise (Autodiscover → EWS)
  - [ ] Gmail / Google Workspace (OAuth2, IMAP/SMTP Google)
  - [ ] IMAP/SMTP générique (repli par défaut)
- [ ] **Mécanismes de détection** (dans l'ordre) :
  - [ ] Fournisseurs connus (gmail.com, outlook.com, etc.)
  - [ ] Enregistrements MX du domaine (ex. `*.mail.protection.outlook.com`, `*.google.com`)
  - [ ] Autodiscover Exchange / Microsoft 365
  - [ ] Autoconfig Thunderbird (`autoconfig.<domaine>`, base ISPDB)
  - [ ] Enregistrements DNS SRV (`_imaps._tcp`, `_submission._tcp`)
- [ ] **Confirmation / correction manuelle** : afficher le type détecté et les paramètres serveur proposés, avec possibilité de forcer un autre type ou de passer en configuration manuelle

## Comptes – Compte par défaut et signatures

- [ ] **Compte par défaut** : dans la liste des comptes (menu Réglages), pouvoir désigner le compte utilisé par défaut :
  - [ ] Pour l'envoi d'un nouveau message (et les liens `mailto:`)
  - [ ] Pour la création d'événements et de contacts (calendrier et carnet d'adresses par défaut)
  - [ ] Pour une réponse / un transfert : utiliser le compte qui a reçu le message (repli sur le compte par défaut)
  - [ ] Indication visuelle du compte par défaut dans la liste
- [ ] **Gestion des signatures** : éditeur de signatures (texte et HTML avec mise en forme, images, liens) dans les Réglages :
  - [ ] Créer / modifier / supprimer plusieurs signatures nommées
  - [ ] Pour chaque compte : signature par défaut pour les nouveaux messages et signature pour les réponses / transferts (ou aucune)
  - [ ] Changement de signature à la volée dans la fenêtre de rédaction, et mise à jour automatique quand on change le compte expéditeur
  - [ ] Placement de la signature (au-dessus / en dessous du texte cité dans les réponses)

## Comptes – Réponses automatiques (Out of Office)

- [ ] **Gestion des réponses automatiques** pour les comptes qui le permettent, accessible depuis *Fichier › Réponses automatiques* et le menu contextuel du compte :
  - [ ] Exchange on-premise (EWS `GetUserOofSettings` / `SetUserOofSettings`)
  - [ ] Microsoft 365 / Exchange Online (Graph `mailboxSettings/automaticRepliesSetting`)
  - [ ] Gmail / Google Workspace (API Gmail *vacation settings*)
  - [ ] IMAP avec Sieve / ManageSieve (extension `vacation`)
  - [ ] Masquer ou griser l'option pour les comptes qui ne la supportent pas
- [ ] **Options** :
  - [ ] Activer / désactiver
  - [ ] Plage de dates (début / fin) ou durée indéterminée
  - [ ] Message distinct pour l'interne (organisation) et l'externe, avec éditeur HTML
  - [ ] Pour l'externe : répondre à tous les expéditeurs ou seulement aux contacts
  - [ ] Exchange / M365 : bloquer le calendrier, refuser automatiquement les nouvelles invitations / annuler les réunions sur la période
- [ ] **Indicateur visible** : bandeau « Réponses automatiques activées pour ce compte » avec bouton *Désactiver* (cf. onglet Fichier / barre d'accès rapide), et rappel à l'ouverture si la période est expirée

## Synchronisation – Progression du rafraîchissement

- [ ] **Barre de progression dans la barre d'état** (en bas de la fenêtre) lors d'un rafraîchissement :
  - [ ] Texte décrivant l'opération en cours (ex. « Compte X – Boîte de réception : chargement des en-têtes 120 / 850 »)
  - [ ] Progression déterminée (pourcentage) quand le nombre total est connu, indéterminée sinon
  - [ ] Progression globale lorsque plusieurs comptes / dossiers sont rafraîchis en parallèle
  - [ ] Bouton pour annuler l'opération en cours
  - [ ] Masquer la barre automatiquement à la fin, avec un message bref en cas d'erreur
- [ ] **Progression dans la barre des tâches Windows** : afficher l'avancement sur l'icône de l'application (ITaskbarList3, état erreur en rouge)

## Vues unifiées multi-comptes

- [ ] **Mails** : une « Boîte de réception unifiée » (et Envoyés, Brouillons, Corbeille unifiés) regroupant les dossiers équivalents de tous les comptes, avec indication du compte d'origine (colonne ou pastille de couleur)
- [ ] **Calendriers** : une vue affichant les événements de tous les calendriers de tous les comptes, avec une couleur par calendrier et la possibilité d'en masquer/afficher chacun
- [ ] **Contacts** : un carnet d'adresses unifié regroupant les contacts de tous les comptes, avec détection et regroupement des doublons (même email)
- [ ] **Recherche dans les vues unifiées** : la recherche simple et la recherche avancée doivent fonctionner sur l'ensemble des comptes affichés dans la vue unifiée, pour chaque type de contenu (mails, événements, contacts), avec le compte d'origine visible dans les résultats
- [ ] **Choix des comptes inclus** : permettre d'exclure certains comptes de la vue unifiée (préférences)

## Calendriers – Affichage multiple et calendriers partagés

- [ ] **Modes d'affichage de plusieurs calendriers** :
  - [ ] **Superposé (vue globale)** : tous les calendriers sélectionnés fusionnés dans une même grille, une couleur par calendrier
  - [ ] **Côte à côte** : une colonne / un volet par calendrier, synchronisés sur la même période (jour, semaine de travail, semaine, mois), avec défilement commun
  - [ ] **Planification** (vue « Scheduling Assistant » d'Outlook) : une ligne par calendrier / personne / ressource, axe horizontal des heures, affichage des créneaux occupés / libres / provisoires / absent, pour trouver un créneau commun
  - [ ] Bascule rapide entre les modes et mémorisation du dernier mode et des calendriers affichés
- [ ] **Ouvrir des calendriers externes** (en particulier Exchange / Microsoft 365) :
  - [ ] Ouvrir le calendrier d'un autre utilisateur (recherche dans le carnet d'adresses global / GAL) selon les droits accordés (disponibilités seules, détails limités, lecture complète, délégué)
  - [ ] Ouvrir plusieurs calendriers partagés simultanément et les conserver dans la liste (groupe « Calendriers partagés »)
  - [ ] Ouvrir des calendriers ICS / CalDAV / webcal par URL
- [ ] **Calendriers de ressources** (salles, équipements) :
  - [ ] Parcourir / rechercher les listes de salles et de ressources Exchange (room lists)
  - [ ] Afficher leur disponibilité en vue Planification
  - [ ] Réserver une ressource lors de la création d'une réunion (ajout comme participant ressource, gestion de l'acceptation / du refus automatique)
- [ ] **Groupes de calendriers** : créer des groupes nommés (ex. « Équipe », « Salles 2e étage ») pour ouvrir plusieurs calendriers d'un coup

## Calendrier – Parité fonctionnelle avec Outlook (ruban)

Mêmes principes que pour la vue Mail : onglets *Accueil*, *Envoi/réception*, *Dossier*, *Affichage*. Hors périmètre : intégrations tierces (Teams, TeamViewer, Rainbow, Copilot).

### Onglet Accueil (Calendrier)

- [ ] **Nouveau** :
  - [ ] Nouveau rendez-vous, Nouvelle réunion (avec participants, salle, vue Planification intégrée)
  - [ ] Ajouter du temps de travail individuel (bloc « focus » marqué occupé, sans participants)
  - [ ] Nouveaux éléments (événement d'une journée entière, événement périodique, tâche, email…)
- [ ] **Réunion en ligne** : bouton générique « Réunion instantanée » / lien de visioconférence, extensible par fournisseur (Teams via Graph pour les comptes M365 en priorité)
- [ ] **Atteindre** : Aujourd'hui, 7 prochains jours, aller à une date (mini-calendrier, Ctrl+G)
- [ ] **Organiser** : Jour, Semaine de travail, Semaine, Mois, Affichage Planification (cf. section Calendriers – Affichage multiple) ; raccourcis Ctrl+Alt+1…5
- [ ] **Emplacement de travail** : indiquer pour chaque jour / créneau le lieu de travail (bureau, télétravail, hors ligne), synchronisé avec Exchange Online si disponible, et visible dans la vue Planification
- [ ] **Gérer les calendriers** :
  - [ ] Ajouter un calendrier : à partir du carnet d'adresses / de l'annuaire, à partir d'une liste de salles, à partir d'Internet (URL ICS / webcal), nouveau calendrier vide, à partir d'un fichier `.ics`
  - [ ] Partager le calendrier : invitation de partage avec niveaux de droits (disponibilités, titres et lieux, tous les détails, édition, délégué), publication en ligne (URL ICS), envoi par email d'un extrait (plage de dates, niveau de détail)
- [ ] **Groupes** : Nouveau groupe, Parcourir les groupes (calendriers de groupes M365)
- [ ] **Rechercher** : recherche rapide de contacts, Carnet d'adresses ; recherche d'événements (cf. section Recherche)

### Onglet Affichage (Calendrier)

- [ ] **Affichage actuel** : Changer l'affichage (Calendrier, Prévisualisation, Liste, Actif — vue liste filtrable des événements à venir), Paramètres d'affichage (champs, tri, filtre, mise en forme conditionnelle par catégorie / organisateur), Rétablir l'affichage
- [ ] **Réorganisation** :
  - [ ] Jour, Semaine de travail, Semaine, Mois (options mois : détails faibles / moyens / élevés), Affichage Planification
  - [ ] Échelle de temps : 5, 6, 10, 15, 30, 60 minutes
  - [ ] Superposition : basculer entre côte à côte et superposé lorsque plusieurs calendriers sont affichés
  - [ ] Heures de travail : n'afficher que les heures / jours de travail (définis dans les options : jours ouvrés, heure de début / fin, premier jour de la semaine, fuseau horaire)
- [ ] **Couleur** : couleur par calendrier (palette), affichée dans la liste et dans la grille
- [ ] **Disposition** :
  - [ ] Utiliser un espacement plus étroit
  - [ ] Liste des tâches quotidiennes (sous la vue Jour / Semaine : tâches et éléments marqués par jour d'échéance)
  - [ ] Volet des dossiers : normal / réduit (avec mini-calendrier de navigation) / désactivé
  - [ ] Volet de lecture : droite / bas / désactivé (aperçu de l'événement sélectionné)
  - [ ] Barre des tâches (To-Do bar) : calendrier, personnes, tâches
- [ ] **Fenêtre** : Fenêtre des rappels, Ouvrir dans une nouvelle fenêtre, Fermer tous les éléments
- [ ] **Lecteur immersif** (pour le corps des invitations)

### Fonctions associées

- [ ] Bandeau « Réponses automatiques » également visible dans la vue Calendrier
- [ ] Barre de navigation de la vue : bouton *Aujourd'hui*, flèches précédent / suivant, plage de dates affichée (ex. « 26 – 30 octobre 2026 »), météo optionnelle
- [ ] Mini-calendrier dans le volet des dossiers (navigation rapide, jours avec événements en gras)
- [ ] Glisser-déposer d'un email vers le calendrier pour créer un rendez-vous ; d'un événement vers un autre jour / calendrier pour le déplacer / copier
- [ ] Fuseaux horaires : afficher un second fuseau horaire dans la grille

## Mail – Parité fonctionnelle avec Outlook (ruban)

Organisation de la barre d'outils / des menus de la vue Mail en onglets inspirés du ruban Outlook : *Accueil*, *Envoi/réception*, *Dossier*, *Affichage*. Hors périmètre : intégrations tierces (Teams, OneNote, TeamViewer, Copilot).

### Barre de titre / accès rapide

- [ ] Barre d'accès rapide personnalisable (Envoyer/Recevoir, Annuler, etc.)
- [ ] Barre latérale gauche de navigation entre modules (Mail, Calendrier, Contacts, Tâches)
- [ ] Bandeau d'information sous la barre d'outils (ex. « Réponses automatiques activées pour ce compte » + bouton *Désactiver*)

### Onglet Accueil

- [ ] **Nouveau** : Nouveau courrier ; Nouveaux éléments (rendez-vous, réunion, contact, tâche, email avec pièce jointe…)
- [ ] **Supprimer** :
  - [ ] Ignorer la conversation (les nouveaux messages du fil vont directement à la Corbeille)
  - [ ] Nettoyer (supprimer les messages redondants d'une conversation / d'un dossier)
  - [ ] Courrier indésirable : bloquer l'expéditeur, jamais bloquer l'expéditeur / le domaine, marquer comme non indésirable, options du filtre
  - [ ] Supprimer, Archiver (déplacer vers le dossier d'archive du compte, touche Retour arrière)
- [ ] **Signaler** : signaler un message comme hameçonnage / spam (rapport à l'administrateur ou au fournisseur)
- [ ] **Répondre** : Répondre, Répondre à tous, Transférer ; Répondre par une réunion ; Transférer en pièce jointe ; Répondre par messagerie instantanée (optionnel)
- [ ] **Actions rapides** : actions personnalisables en un clic (déplacer vers un dossier + marquer comme lu, transférer à un responsable, message d'équipe, etc.), éditeur d'actions rapides
- [ ] **Déplacer** : Déplacer vers un dossier (liste des derniers dossiers utilisés), Copier vers un dossier
- [ ] **Règles** : créer une règle à partir du message sélectionné, gérer les règles et alertes (règles côté serveur pour Exchange / Sieve si disponible, côté client sinon)
- [ ] **Indicateurs / Catégories** :
  - [ ] Lu / Non lu
  - [ ] Catégoriser (catégories en couleur, partagées avec Exchange si possible)
  - [ ] Assurer un suivi (aujourd'hui, demain, cette semaine, semaine prochaine, sans date, personnalisé, ajouter un rappel, marquer comme terminé, effacer l'indicateur)
- [ ] **Groupes** : Nouveau groupe, parcourir les groupes (groupes Microsoft 365, si compte M365)
- [ ] **Rechercher** : recherche rapide de contacts (champ dans la barre), Carnet d'adresses, *Filtre de courrier* (non lus, avec pièces jointes, cette semaine, marqués, importants, mentionné…) — cf. sections Recherche et Filtres rapides
- [ ] **Fonction vocale** : Lecture à voix haute du message (synthèse vocale Windows SAPI / WinRT)

### Onglet Envoi/réception

- [ ] **Envoyer et recevoir** : Envoyer/Recevoir tous les dossiers (F9), Mettre à jour le dossier courant (Maj+F9), Envoyer tout (boîte d'envoi)
- [ ] **Groupes d'envoi/réception** : définir des groupes de comptes / dossiers avec planification de synchronisation (intervalle, à la fermeture, en mode hors connexion)
- [ ] **Télécharger** : Afficher la progression (cf. section Progression), Annuler tout
- [ ] **Serveur** (comptes IMAP / Exchange en mode en-têtes seuls) :
  - [ ] Télécharger les en-têtes
  - [ ] Marquer pour téléchargement (message / copie), annuler les marques, traiter les en-têtes marqués
- [ ] **Préférences** :
  - [ ] Préférences de téléchargement (en-têtes seuls / message complet, limite de taille, uniquement en connexion lente)
  - [ ] Travailler en mode hors connexion (bascule globale, envois mis en file d'attente)

### Onglet Dossier

- [ ] **Nouveau** : Nouveau dossier, Nouveau dossier de recherche (modèles prédéfinis : non lus, marqués, de personnes spécifiques, gros messages…)
- [ ] **Actions** : Renommer, Copier, Déplacer, Supprimer le dossier (drag & drop dans l'arborescence)
- [ ] **Nettoyer** :
  - [ ] Marquer tout comme lu
  - [ ] Exécuter les règles maintenant sur le dossier
  - [ ] Afficher tous les dossiers de A à Z (tri alphabétique de l'arborescence)
  - [ ] Nettoyer le dossier (et sous-dossiers) des messages redondants
  - [ ] Supprimer tout (vider le dossier, vider la Corbeille / les indésirables)
  - [ ] Récupérer les éléments supprimés (Exchange : *Recoverable Items* ; IMAP : messages marqués supprimés non purgés)
- [ ] **Favoris** : Ajouter aux favoris (section *Favoris* en haut du volet des dossiers, réordonnable)
- [ ] **Affichage en ligne** : Afficher dans le navigateur (OWA / webmail du fournisseur)
- [ ] **Propriétés** :
  - [ ] Stratégie de rétention (Exchange)
  - [ ] Paramètres d'archivage automatique (âge des éléments, déplacement vers un dossier / fichier d'archive, suppression)
  - [ ] Autorisations du dossier (partage / délégation Exchange, ACL IMAP)
  - [ ] Propriétés du dossier (taille, nombre d'éléments, afficher le nombre total / non lus, quota du compte)

### Onglet Affichage

- [ ] **Affichage actuel** : Changer l'affichage (Compact, Unique, Aperçu), Paramètres d'affichage (colonnes, tri, regroupement, filtre, polices, mise en forme conditionnelle), Rétablir l'affichage par défaut
- [ ] **Messages** : Afficher en tant que conversations, Paramètres de conversation (afficher les messages d'autres dossiers, afficher les expéditeurs au-dessus de l'objet, toujours développer la conversation sélectionnée, retrait classique) — cf. section Discussions
- [ ] **Boîte de réception Prioritaire** : onglets *Prioritaire* / *Autres* (utilise le classement Exchange Online si disponible)
- [ ] **Réorganisation** :
  - [ ] Aperçu du message (désactivé, 1, 2 ou 3 lignes sous l'objet)
  - [ ] Organiser par : Date (conversations), De, À, Catégories, État de l'indicateur, Indicateur (date de début / d'échéance), Taille, Objet, Type, Pièces jointes, Compte, Importance
  - [ ] Inverser le tri, Ajouter des colonnes, Développer / Réduire les groupes — cf. section Tri
- [ ] **Disposition** :
  - [ ] Utiliser un espacement plus étroit (mode compact)
  - [ ] Volet des dossiers : normal / réduit / désactivé
  - [ ] Volet de lecture : droite / bas / désactivé — cf. section Disposition des panneaux
  - [ ] Barre des tâches (To-Do bar) : calendrier, personnes, tâches à venir, affichée à droite
- [ ] **Fenêtre** : Fenêtre des rappels, Ouvrir dans une nouvelle fenêtre, Fermer tous les éléments ouverts
- [ ] **Lecteur immersif** : vue de lecture plein écran épurée (taille du texte, espacement, lecture à voix haute)

### Onglet Fichier (Backstage)

- [ ] Informations du compte : paramètres du compte, réponses automatiques (cf. section Réponses automatiques), outils de nettoyage de la boîte aux lettres, quota
- [ ] Ouvrir et exporter : ouvrir un calendrier / fichier de données, importer / exporter (cf. section Export)
- [ ] Enregistrer sous, Enregistrer les pièces jointes, Imprimer (aperçu, styles mémo / tableau)
- [ ] Options (préférences générales de l'application)

## Application – Icône

- [ ] **Icône de l'exécutable** : créer un fichier `.ico` multi-résolution (16, 24, 32, 48, 64, 256 px) et l'intégrer comme ressource dans `EvolutionWin.exe`
  - [ ] Utilisée par l'Explorateur, la barre des tâches, Alt+Tab et la barre de titre de la fenêtre (`AppWindow.SetIcon`)
  - [ ] Raccourci du menu Démarrer (installeur MSI)
  - [ ] Icône dans *Paramètres › Applications installées* (propriété `ARPPRODUCTICON` du MSI)

## Export des données d'un compte (formats ouverts)

- [ ] **Assistant d'export** (menu *Fichier › Exporter*) : choisir le compte, puis les éléments à exporter (emails, contacts, calendriers, tâches) et le dossier de destination
- [ ] **Emails** :
  - [ ] Format mbox (un fichier par dossier, arborescence conservée)
  - [ ] Format EML (un fichier `.eml` par message, arborescence de dossiers conservée)
  - [ ] Choix des dossiers à exporter (tous / sélection / dossier courant)
- [ ] **Contacts** : format vCard (`.vcf`, un fichier unique ou un fichier par contact) ; CSV en option
- [ ] **Calendriers et tâches** : format iCalendar (`.ics`, un fichier par calendrier)
- [ ] **Options** : filtre par plage de dates, export en arrière-plan avec progression dans la barre d'état, rapport de fin (nombre d'éléments exportés, erreurs)
