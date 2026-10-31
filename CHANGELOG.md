# Journal des modifications

Toutes les évolutions notables d'EvolutionWin (portage natif Windows 11
d'Evolution) sont décrites ici, par version. La version en cours est dans
le fichier [`VERSION`](VERSION) ; c'est aussi celle de l'installeur
(`tools\build-msi.bat`). Le détail technique de chaque étape est dans
[`WINDOWS-PORT.md`](WINDOWS-PORT.md), les tâches restantes dans
[`TODO.md`](TODO.md).

Format : [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/),
numérotation [SemVer](https://semver.org/lang/fr/).

## [0.9.3] — en cours

### Ajouté

- **Barre de lecture rapide** à gauche de chaque message (comme Outlook) :
  couleur d'accent pour les non-lus ; un clic bascule lu / non lu sans
  ouvrir ni sélectionner le message, info-bulle au survol.
- **Filtres rapides** au-dessus de la liste : Tous / Non lus / Lus, avec
  indicateur, avec pièces jointes, cette semaine ; combinables entre eux et
  avec la recherche.
- **Menu contextuel des dossiers** (clic droit) : *Marquer tout comme lu* ;
  *Vider le dossier* (vers la Corbeille), *Vider la Corbeille* et *Vider les
  Indésirables* (suppression définitive), avec confirmation.
- **Enregistrer sous** (Ctrl+S) : message au format `.eml` ; **Tout
  enregistrer** dans la barre des pièces jointes (dossier au choix, noms en
  double numérotés).
- **Compte par défaut** : badge « Par défaut » et bouton *Définir par
  défaut* dans Paramètres › Comptes ; les nouveaux messages partent de ce
  compte, les réponses et transferts du compte qui a reçu le message.
- **Panneaux redimensionnables** dans la vue Courrier : séparateurs
  déplaçables entre les dossiers, la liste des messages et le volet de
  lecture (curseur de redimensionnement, double-clic pour la largeur par
  défaut) ; largeurs mémorisées d'une session à l'autre.
- Bandeau **« Contenu externe bloqué »** au-dessus du message lorsque des
  images, styles ou pixels de suivi distants ont été bloqués : *Afficher les
  images pour ce message*, *Toujours faire confiance à l'expéditeur*, *Toujours
  faire confiance au domaine*.
- **Paramètres › Confidentialité** : chargement du contenu externe (jamais /
  expéditeurs et domaines de confiance seulement / toujours) et liste des
  expéditeurs et domaines de confiance.
- Lecteur : chaque message est chargé à une adresse interne unique (plus de
  limite de 2 Mo), redirections automatiques bloquées, pas de Referer.
- **Export des contacts** en vCard 3.0 (un fichier ou un par contact, un
  carnet ou tous) et **des agendas et listes de tâches** en iCalendar avec
  leurs fuseaux horaires (vues Contacts et Calendrier).
- **Module Tâches** (barre latérale) : liste par liste de tâches avec objet,
  échéance (en rouge si en retard), priorité, état, pourcentage et
  catégories ; case « terminée », masquage des tâches terminées, filtres
  Aujourd'hui / En retard / Cette semaine / Sans date, tri par échéance ou
  priorité ; éditeur de tâche ; nouvelle tâche avec Ctrl+Maj+K.
- **Gestion des dossiers** (clic droit dans l'arborescence) : nouveau
  dossier (aussi sur un compte), renommer, déplacer vers, copier vers,
  supprimer le dossier (avec confirmation) ; dossiers spéciaux protégés.
- **Signatures** : éditeur dans les Paramètres (texte ou HTML avec
  mise en forme, liens et images), plusieurs signatures nommées, choix par
  compte pour les nouveaux messages et pour les réponses / transferts,
  position au-dessus ou en dessous du texte cité, sélecteur dans la fenêtre
  de rédaction ; images des signatures envoyées en parties liées (cid:).

### Corrigé

- Aperçu vidé (« Sélectionnez un message pour le lire ») juste après la
  suppression d'un message, alors que le message suivant venait de s'y
  afficher : le rechargement de la liste désélectionnait le message ;
  idem en changeant de filtre rapide.
- Plantage au renommage d'un dossier IMAP fermé (evolution-data-server) :
  le dossier est gardé ouvert pendant le renommage.
- Signatures illisibles sous Windows (evolution-data-server tentait de les
  exécuter comme scripts) : fichier lu directement.
- Le compte se repliait dans l'arborescence après une opération sur un
  dossier.

## [0.9.2] — 2026-10-31

### Ajouté

- Icône de l'application (celle d'Evolution) : exécutable, barre de titre,
  barre des tâches, Alt+Tab, raccourci du menu Démarrer et « Applications
  installées ».
- Fichiers `VERSION` et `CHANGELOG.md` ; `build-msi.bat` prend sa version
  dans `VERSION` par défaut.
- Boîte **À propos** (en bas de la barre latérale) : version, licence,
  liens vers le dépôt et ce journal, versions des composants
  (evolution-data-server, evolution-ews, GLib, libsoup, Windows App SDK,
  WebView2, Windows) et bouton « Copier les informations » pour les
  rapports de bug.
- Version de l'exécutable (propriétés du fichier) tirée de `VERSION`, comme
  l'installeur et la boîte À propos.
- **Envoyer/Recevoir tous les dossiers** (F9), avec la progression dans la
  barre d'état, et **Mettre à jour le dossier courant** (Maj+F9) ; menu
  déroulant sur le bouton d'actualisation.

### Corrigé

- Mise à jour ou désinstallation pendant qu'Evolution est ouvert :
  l'installeur ferme EvolutionWin et les services evolution-data-server au
  lieu d'afficher un dialogue « fichiers en cours d'utilisation » (souvent
  caché, d'où l'erreur 1618 si l'installation était relancée).

## [0.9.1] — 2026-10-31

### Corrigé

- Boîte de réception vide au premier lancement (installation neuve ou compte
  tout juste ajouté) : la relève de démarrage ouvrait une seconde instance du
  dossier, que le serveur IMAP ne remplissait pas.
- Connexions simultanées d'un même compte : une relève lancée pendant la
  connexion était ignorée sans erreur ; les connexions sont désormais
  sérialisées.

## [0.9.0] — 2026-10-31

### Ajouté

- Comptes **Microsoft 365 / Exchange Online** par Microsoft Graph
  (evolution-ews compilé avec MSVC) : courrier, agendas et carnets
  d'adresses.
- Connexion OAuth2 dans une fenêtre WebView2 (identifiant d'application et
  tenant facultatifs) ; jetons enregistrés dans le Gestionnaire
  d'identifiants Windows et renouvelés automatiquement ; nouvelle connexion
  proposée si le jeton est révoqué.
- Installeur : connecteurs Microsoft 365 et EWS, traductions d'evolution-ews.

### Modifié

- Suppression d'un compte Microsoft 365 : agendas, carnets d'adresses et
  jeton supprimés avec lui.

## [0.8.0] — 2026-10-31

Première version distribuée sous forme d'installeur.

### Ajouté

- **Installeur MSI** autonome (WiX v5), par machine, déployable
  silencieusement (`msiexec /i … /qn`) : EvolutionWin, services
  evolution-data-server et toutes leurs dépendances, runtime Visual C++
  inclus ; raccourci du menu Démarrer ; vérification du runtime WebView2.
- Client natif **WinUI 3** (`EvolutionWin.exe`) sur le moteur
  d'Evolution (evolution-data-server, Camel), compilé avec MSVC :
  - courrier IMAP / SMTP : dossiers et compteurs de non-lus, liste et
    lecture des messages (WebView2, contenu distant bloqué), relève
    automatique et notifications Windows ;
  - rédaction texte et HTML, réponses, transferts, pièces jointes,
    impression ;
  - actions sur les messages (lu / non lu, drapeau, déplacer, supprimer),
    sélection multiple, recherche (en-têtes et corps) ;
  - calendrier et contacts d'evolution-data-server ;
  - gestion des comptes, mots de passe dans le Gestionnaire d'identifiants
    Windows, dialogue de certificats TLS ;
  - S/MIME et OpenPGP (Gpg4win) à la lecture et à l'envoi.

[0.9.3]: https://github.com/pitch7900/Evolution-WindowsPort/compare/v0.9.2...HEAD
[0.9.2]: https://github.com/pitch7900/Evolution-WindowsPort/compare/v0.9.1...v0.9.2
[0.9.1]: https://github.com/pitch7900/Evolution-WindowsPort/compare/v0.9.0...v0.9.1
[0.9.0]: https://github.com/pitch7900/Evolution-WindowsPort/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/pitch7900/Evolution-WindowsPort/releases/tag/v0.8.0
