# Journal des modifications

Toutes les évolutions notables d'EvolutionWin (portage natif Windows 11
d'Evolution) sont décrites ici, par version. La version en cours est dans
le fichier [`VERSION`](VERSION) ; c'est aussi celle de l'installeur
(`tools\build-msi.bat`). Le détail technique de chaque étape est dans
[`WINDOWS-PORT.md`](WINDOWS-PORT.md), les tâches restantes dans
[`TODO.md`](TODO.md).

Format : [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/),
numérotation [SemVer](https://semver.org/lang/fr/).

## [0.9.6] — en cours

### Corrigé

- **Stabilité au démarrage et élimination définitive du crash post-splashscreen (0x80004002 / 0xc0000005)** :
  - **Correction du gabarit XAML de MessageList et EventList** : suppression des setters `MinHeight` et `Padding` dans `ItemContainerStyle` qui provoquaient une incompatibilité d'interface (`E_NOINTERFACE 0x80004002`) lors de la mesure des conteneurs par le dispatcher XAML.
  - **Protection contre les événements prématurés et la réentrance** : ajout systématique de gardes `if (!m_initialized)` dans les gestionnaires de paramètres (`MainWindow.Settings.cpp`) et de mise en page (`MainWindow.Layout.cpp`) pour neutraliser tout déclenchement d'événement XAML (`Toggled`, `SelectionChanged`) avant la finalisation de l'initialisation de la fenêtre.
  - **Suppression de l'appel anticipé de statut réseau** : élimination de l'appel `UpdateConnectionStatus` dans `SetSplashWindow` avant que la fenêtre ne soit prête, prévenant toute corruption de mémoire sur les conteneurs d'état de compte.
  - **Nettoyage et régénération des binaires intermédiaires** : recompilation propre de l'ensemble des modules assurant un alignement parfait de l'ABI et des structures en mémoire. L'application démarre désormais de manière fluide et pérenne.

### Ajouté

- **Bascule du mode de tri des dossiers depuis le menu contextuel du compte (TODO ligne 533)** :
  - **Menu contextuel enrichi** : ajout d\'un sous-menu « Ordre des dossiers » sur chaque compte dans l\'arborescence (MainWindow.Folders.cpp).
  - **Choix par compte** : bascule directe entre « Dossiers spéciaux d\'abord », « Trier tous les dossiers de A à Z » et « Rétablir l\'ordre global par défaut » avec coches d\'état visuelles (✔).
  - **Priorité de configuration** : prise en compte de la surcharge par compte (ccount.<account>.folder-sort) avec repli fluide sur la préférence globale mail.folder-sort.
  - **Validation par tests unitaires** : vérification de la persistance et de la réinitialisation du tri par compte dans 	ests/unit/test_settings.cpp.

- **Second fuseau horaire dans la grille du calendrier (TODO ligne 605)** :
  - **Affichage bilatéral des créneaux horaires** : intégration dans la gouttière horaire gauche (MainWindow.Calendar.cpp) d\'une double colonne d\'heures (fuseau secondaire et heure locale) avec séparateur visuel discret.
  - **En-têtes clairs des fuseaux** : affichage au sommet de la colonne horaire des abréviations courtes correspondantes (UTC, NYC, LA, PAR, LON, TYO ou libellé personnalisé).
  - **Calculs et conversions automatiques** : algorithmes dédiés (GetTimezoneUtcOffsetHours, GetTimezoneShortLabel, CalculateEquivalentHour, GetLocalUtcOffsetHours) avec prise en compte robuste des décalages et des passages de minuit.
  - **Validation par tests unitaires** : ajout de 3 nouveaux tests complets dans 	ests/unit/test_helpers.cpp (40/40 tests validés).

- **Aperçu du message sous l\'objet et masquage confidentiel des extraits (TODO lignes 398 et 843)** :
  - **Extraction native du texte** : exposition du champ preview (extrait du corps via camel_message_info_get_preview) dans les listes de messages et les recherches transversales vo-core.
  - **Affichage configurable** : sélection de 0 (désactivé), 1, 2 ou 3 lignes d\'extrait sous l\'objet du message dans *Paramètres › Courrier › Lecture et affichage* (mail.snippet-lines).
  - **Mode confidentialité instantané** : prise en compte directe de l\'interrupteur privacy.hide-snippets pour masquer tous les extraits et préserver la discrétion à l\'écran.
  - **Mise à jour visuelle en direct** : rafraîchissement immédiat de la liste de messages lors de la modification des réglages sans rechargement lourd.
  - **Validation par tests unitaires** : couverture des préférences mail.snippet-lines et privacy.hide-snippets dans 	ests/unit/test_settings.cpp.

- **Relève automatique du courrier au démarrage de l'application (TODO ligne 829)** :
  - **Exécution au lancement** : synchronisation immédiate de tous les dossiers de tous les comptes dès la fin de l'initialisation des comptes (`mail.sync-on-startup`).
  - **Contrôle utilisateur** : case à cocher active dans *Paramètres › Courrier* pour activer ou désactiver cette relève initiale.
  - **Validation par tests unitaires** : persistance de `mail.sync-on-startup` vérifiée dans `tests/unit/test_settings.cpp`.

- **Signaux sonores de notification paramétrables (TODO ligne 816)** :
  - **Choix du signal sonore** : sélection entre le son système Windows officiel de nouveau courrier (`MailBeep` / `PlaySoundW`), un carillon discret (`chime`) ou aucun son.
  - **Bouton d'écoute directe** : commande « Tester » dans *Paramètres › Notifications* pour écouter immédiatement le son sélectionné.
  - **Émission asynchrone native** : intégration dans la routine d'arrivée de nouveaux messages `ShowNewMailNotification` sans blocage de l'interface utilisateur.
  - **Validation par tests unitaires** : persistance de `notifications.sound-kind` dans `tests/unit/test_settings.cpp`.

- **Effacement réel des caches WebView2 et aperçus temporaires (TODO ligne 845)** :
  - **Purge complète des caches de rendu** : appel direct à `ClearBrowsingDataAsync` sur le profil WebView2 du lecteur de messages.
  - **Suppression des fichiers temporaires d'aperçus** : nettoyage intégral et sécurisé du répertoire `%TEMP%\EvolutionWin\Previews` via `PurgeDirectoryFiles`.
  - **Bilan d'espace libéré** : calcul dynamique de la taille récupérée et notification informative à l'utilisateur (« Caches vidés : X Ko/Mo libérés »).
  - **Validation par tests unitaires** : suite de tests `PurgeDirectoryFiles` ajoutée dans `tests/unit/test_helpers.cpp` (37/37 tests validés).

- **Calendrier et carnet d'adresses par défaut pour les nouveaux éléments (TODO ligne 271)** :
  - **Sélecteur de calendrier par défaut** : ajout d'une option `calendar.active` dans *Paramètres › Calendrier* pour désigner l'agenda d'enregistrement par défaut des nouveaux rendez-vous et réunions parmi les sources configurées.
  - **Sélecteur de carnet d'adresses par défaut** : ajout d'une option `contacts.active` dans *Paramètres › Contacts* pour choisir le carnet principal d'ajout des nouveaux contacts.
  - **Synchronisation dynamique avec l'interface** : mise à jour instantanée des bandeaux d'indication et des panneaux de sources lors d'une modification des réglages.
  - **Validation par tests unitaires** : ajout des assertions de persistance dans `tests/unit/test_settings.cpp`.

- **Fermer tous les éléments ouverts (TODO ligne 188)** :
  - **Gestion unifiée des fenêtres secondaires** : recensement dynamique et suivi du cycle de vie de toutes les fenêtres de lecture indépendante (`MessageWindow`) et de rédaction de messages (`ComposeWindow`).
  - **Commande dédiée dans le ruban** : bouton `Fermer les fenêtres ouvertes` avec icône dans le ruban Affichage (Courrier et Calendrier) pour refermer instantanément toutes les fenêtres secondaires et ne conserver que la fenêtre principale.
  - **Fermeture propre à l'arrêt** : intégration dans l'événement `Closed` de `MainWindow` pour garantir l'arrêt sans blocage ni fuite de processus en arrière-plan.

- **Ordre des comptes et tri des dossiers dans le volet latéral (Quickwin §1 / §6 / TODO ligne 534)** :
  - **Priorité au compte principal** : le compte de messagerie par défaut est automatiquement positionné en tête de liste dans l'arborescence des dossiers, suivi des autres comptes distants par ordre alphabétique, et enfin des dossiers locaux (« Sur cet ordinateur »).
  - **Tri paramétrable des dossiers** : option permettant de choisir entre l'affichage des dossiers spéciaux en premier (`special-first`, Boîte de réception, Brouillons, Éléments envoyés...) et le tri alphabétique complet de A à Z (`alphabetical`).
  - **Réglage dédié dans les paramètres** : sélecteur direct `mail.folder-sort` dans *Paramètres › Courrier* avec actualisation dynamique de l'arborescence.
  - **Validation par tests unitaires** : tests de persistance de `mail.folder-sort` dans `tests/unit/test_settings.cpp`.

- **Volet des dossiers réductible en Mail et Calendrier (Quickwin §1 / §2 / TODO ligne 181)** :
  - **Trois modes d'affichage ergonomiques** : Normal (pleine largeur dépliée mémorisée), Réduit (bande compacte de 48px pour les icônes de dossiers ou affichage exclusif du mini-calendrier) et Désactivé (volet entièrement masqué à 0px pour maximiser l'espace d'affichage).
  - **Menu déroulant dans le ruban** : bouton `Volet des dossiers` avec options radio (Normal, Réduit, Désactivé) accessible aussi bien dans le ruban Courrier que dans le ruban Calendrier.
  - **Option dédiée dans les paramètres** : sélecteur direct `layout.folder-pane` dans *Paramètres › Apparence* synchronisé avec l'interface.
  - **Validation par tests unitaires** : couverture des transitions d'états dans `tests/unit/test_settings.cpp`.

- **Échelle de temps du calendrier (Quickwin §4 / §12 / TODO ligne 603)** :
  - **Intervalles configurables** : sélection rapide du pas horaire dans la grille Jour / Semaine de travail / Semaine (60 minutes, 30 minutes ou 15 minutes).
  - **Commande dédiée dans le ruban Calendrier** : menu déroulant `RibbonCalendarTimeScale` avec icône d'horloge pour changer dynamiquement l'échelle d'affichage sans quitter la vue active.
  - **Configuration dans les paramètres** : synchronisation bidirectionnelle avec la préférence `calendar.time-scale` dans les paramètres d'affichage du calendrier.
  - **Calcul dynamique de la hauteur des créneaux** : fonction d'assistance `GetCalendarHourHeight` calculant la hauteur optimale de chaque créneau horaire (48px pour 60 min, 72px pour 30 min, 120px pour 15 min) et garantissant la lisibilité des événements courts.
  - **Validation par tests unitaires** : ajout de la suite `CalendarHourHeight` dans `tests/unit/test_helpers.cpp` (36/36 tests validés).

- **Recherche rapide de contacts et accès direct au carnet d'adresses (Quickwin §4 / §14)** :
  - **Champ dédié dans le ruban Accueil** : intégration d'une `AutoSuggestBox` dans le ruban principal de la messagerie (style Outlook) permettant de rechercher n'importe quel contact instantanément.
  - **Suggestions dynamiques en direct** : affichage en temps réel des correspondances avec nom complet et adresse électronique au fur et à mesure de la saisie via `ListBookContacts`.
  - **Navigation fluide vers le carnet d'adresses** : la validation ou la sélection d'un contact bascule automatiquement vers le module Contacts avec le filtre appliqué (`ContactSearchBox`).
  - **Bouton d'accès direct « Carnet d'adresses »** : commande dédiée dans le ruban pour ouvrir immédiatement le carnet d'adresses complet d'un simple clic.
  - **Validation par tests unitaires** : ajout de la suite `FormatContactSuggestion` dans `tests/unit/test_helpers.cpp` (33/33 tests validés).

- **Aperçus sécurisés de pièces jointes simples (Quickwin §2 / §8)** :
  - **Aperçu intégré dans le volet de lecture et la fenêtre de message** : clic direct sur le bouton de n'importe quelle pièce jointe pour basculer instantanément vers un panneau d'aperçu dédié avec bouton « Retour au message », nom du document et boutons d'action (« Enregistrer sous... », « Ouvrir »).
  - **Images supportées** : décodage natif et sécurisé via `BitmapImage` (PNG, JPEG, GIF, BMP, WebP, ICO, TIFF) avec adaptation fluide aux dimensions de l'affichage.
  - **Fichiers texte, code et données** : affichage en texte brut sécurisé sans aucune interprétation de code pour les fichiers texte (`.txt`, `.log`, `.csv`, `.tsv`, `.json`, `.xml`, `.ini`, `.conf`, `.md`, `.patch`, `.sql`, code source C/C++/C#/Python/JS/TS).
  - **Protection des types exécutables à risque** : désactivation automatique de l'aperçu et présentation d'une fiche d'avertissement pour les fichiers exécutables ou non reconnus (`.exe`, `.bat`, `.ps1`, archives, etc.).
  - **Menu contextuel enrichi** : clic droit sur chaque pièce jointe offrant le choix direct entre « Aperçu » et « Enregistrer sous... ».
  - **Validation par tests unitaires** : suite de tests `DetectAttachmentPreviewKind` ajoutée dans `tests/unit/test_helpers.cpp` (29/29 tests validés).

- **Lecture à voix haute du message (`Windows.Media.SpeechSynthesis`) (Quickwin §3 / §18)** :
  - **Synthèse vocale native Windows** : intégration de l'API moderne `Windows.Media.SpeechSynthesis` et `Windows.Media.Playback.MediaPlayer` pour lire le contenu des courriels à voix haute avec les voix système installées sous Windows 11.
  - **Commandes dédiées** : bouton « Lire à voix haute » avec glyphe officiel Fluent (`\xEC92`) dans la barre d'outils du volet de lecture principal et de la fenêtre indépendante (`MessageWindow`), basculant dynamiquement vers un bouton d'arrêt/pause (`\xE71A`) pendant l'écoute.
  - **Extraction et nettoyage de contenu** : utilitaire `ExtractPlainTextForSpeech` extrayant l'en-tête (expéditeur, objet) et nettoyant les balises HTML et entités encodées pour une élocution naturelle et fluide.
  - **Tests unitaires de validation** : tests unitaires d'extraction de texte et de nettoyage HTML ajoutés à `tests/unit/test_helpers.cpp` (26/26 tests réussis).

- **Intégration Windows 11 : Progression et badge sur la barre des tâches (`ITaskbarList3`) (Quickwin §3 / §11)** :
  - **Jauges de progression dynamiques** : affichage de la progression sur l'icône de la barre des tâches lors des synchronisations de boîte ou opérations d'arrière-plan (vert pour déterminé/indéterminé, rouge temporaire en cas d'erreur réseau ou d'envoi).
  - **Badge de courriels non lus (overlay icon)** : génération dynamique d'une icône de superposition 16x16 représentant une pastille bleue Fluent avec le nombre exact de messages non lus (ou `99+`), mis à jour instantanément avec le badge du volet de navigation.
  - **Respect des préférences utilisateur** : prise en compte de la clé `notifications.taskbar-badge` (`true` par défaut), validée par test unitaire dans `tests/unit/test_settings.cpp`.

- **Bouton « Afficher dans le navigateur » (Quickwin §2 / §7)** :
  - **Ouverture native et ergonomique** : ajout d'un bouton dédié avec icône globe (`\xE774`) dans la barre d'outils du volet de lecture (`ReaderPane`), dans la fenêtre indépendante (`MessageWindow`) et dans le menu contextuel par clic droit sur un message.
  - **Rendu HTML autonome et soigné** : routine `BuildStandaloneHtmlMessage` générant un document HTML 5 complet et stylisé (en-tête avec expéditeur, destinataires, date et objet, corps HTML ou texte brut formaté) enregistré dans un emplacement temporaire sécurisé (`%TEMP%\EvolutionWin`).
  - **Lancement dans le navigateur par défaut** : ouverture immédiate via `ShellExecuteW` dans le navigateur Web système (Edge, Chrome, Firefox, etc.) facilitant la consultation des newsletters complexes et courriels transactionnels.
  - **Tests unitaires de validation** : validation de la génération autonome et de l'échappement sécurisé (`test_build_standalone_html_message`) dans `tests/unit/test_helpers.cpp` (24/24 tests réussis).

- **Confirmations de sécurité pour la rédaction et la suppression (Quickwin §5 / §19)** :
  - **Avertissement de pièce jointe oubliée** : analyse syntaxique du sujet et du corps du message dans `ComposeWindow` via `HasAttachmentKeywords` (mots-clés français et anglais comme "pièce jointe", "ci-joint", "en pj", "attachment"). Si aucun fichier n'est joint, une boîte modale Fluent avertit l'expéditeur avant le départ du message.
  - **Avertissement d'envoi sans objet** : confirmation modale automatique si l'objet est vide et que `mail.warn-empty-subject` est actif (activé par défaut).
  - **Confirmation avant suppression définitive (Maj+Suppr et Corbeille)** : interception du raccourci clavier `Suppr` et de la combinaison `Maj+Suppr` sur la liste des messages. Si la suppression est définitive ou effectuée depuis la Corbeille, un dialogue de sécurité `ConfirmAndDeleteMessages()` empêche toute perte accidentelle tant que `mail.confirm-permanent-delete` est actif.
  - **Test unitaire dédié** : ajout du test `test_attachment_keywords` dans `tests/unit/test_helpers.cpp` (22/22 tests réussis).

- **Vider automatiquement la Corbeille à la fermeture (Quickwin §5 / §19)** :
  - **Optionnel via case à cocher dans les Paramètres** : ajout d'une case à cocher standard (`CheckBox` x:Name `MailEmptyTrashOnExitCheck`) dans *Paramètres › Courrier* reliée à la préférence `mail.empty-trash-on-exit` (`false` par défaut).
  - **Purge complète multi-comptes** : lors de la fermeture de la fenêtre principale, si la case est cochée, `EmptyAllTrashFolders()` parcourt l'ensemble des comptes et des dossiers spéciaux pour envoyer la commande de purge définitive `empty` à `evo-core`.
  - **Déduplication sécurisée** : utilisation d'un registre d'UIDs pour éviter tout double déclenchement sur les dossiers mappés.
  - **Validation par test unitaire** : couverture de la clé de configuration et de son repli sécurisé (`false` par défaut) dans `tests/unit/test_settings.cpp`.

- **Gestion complète du statut « Lu / Non lu » (Quickwin §5 / §19)** :
  - **Quatre modes de marquage paramétrables** : configuration via *Paramètres › Courrier* avec prise en charge complète des options *Immédiatement*, *Après 5 secondes*, *Au changement de sélection* et *Manuellement seulement* (`mail.mark-as-read`).
  - **Minuterie asynchrone non-bloquante** : implémentation d'un `DispatcherTimer` dédié pour le mode différé (5 secondes) avec annulation automatique si l'utilisateur change de sélection ou vide la liste avant la fin du délai.
  - **Prise en charge du mode au changement de sélection** : marquage automatique du message précédemment consulté dès qu'un nouveau message est sélectionné ou que le volet de lecture est libéré.
  - **Synchronisation dynamique de l'indicateur serveur** : transmission réelle de l'indicateur `seen` vers `evo-core` et le serveur IMAP/Exchange distant via `SetMessageFlag`.
  - **Validation par test unitaire dédié** : ajout du test `test_settings_mail_read_and_security_options` dans la suite unitaire `tests/unit/test_settings.cpp`.

- **Sélection du module actif au démarrage (Quickwin §5 / §19)** :
  - **Prise en compte automatique à l'ouverture** : application au lancement du réglage `appearance.startup-view` défini dans *Paramètres › Apparence* (Courrier, Calendrier, Contacts ou Tâches).
  - **Transition fluide** : sélection directe du module cible via `SelectNavigationModule` et masquage immédiat des autres vues sans clignotement à l'affichage.
- **Atteindre une date dans le Calendrier (Ctrl+G - Quickwin §4 / §12)** :
  - **Boîte de dialogue modale Windows 11 Fluent** : accessible via le raccourci clavier global Ctrl+G, le ruban Calendrier (*Atteindre la date*) ou le bouton rapide « Aller au… » au-dessus de la grille des rendez-vous.
  - **Sélecteur de date graphique (CalendarDatePicker)** : prépositionné sur la date actuellement visualisée pour une navigation rapide vers n'importe quelle date passée ou future.
  - **Raccourcis en un clic** : boutons « Aujourd'hui » et « +7 jours » intégrés dans la boîte de dialogue pour une navigation ultra rapide.
  - **Bascule automatique de module** : si la commande est appelée depuis Courrier, Contacts ou Tâches, l'application bascule automatiquement vers le Calendrier et se positionne directement sur la date choisie.
- **Raccourcis de navigation globale et vues du calendrier (Quickwin §4 / §12)** :
  - **Navigation instantanée entre modules (Ctrl+1..4 et Ctrl+Alt+1..4)** : bascule directe depuis n'importe quelle vue vers Courrier (Ctrl+1), Calendrier (Ctrl+2), Contacts (Ctrl+3) et Tâches (Ctrl+4).
  - **Bascule rapide des vues Calendrier (Ctrl+Alt+1..5)** : dans la vue Calendrier, les combinaisons Ctrl+Alt commutent instantanément l'affichage entre Jour (Ctrl+Alt+1), Semaine de travail (Ctrl+Alt+2), Semaine (Ctrl+Alt+3), Mois (Ctrl+Alt+4) et Liste (Ctrl+Alt+5).
  - **Infobulles enrichies** : affichage explicite des raccourcis clavier dans les infobulles des modules de navigation (NavigationView) et des boutons de vue du ruban Calendrier.
- **Mode compact des listes (Quickwin #3)** :
  - **Espacement plus étroit pour afficher plus d'éléments** : réduction de la hauteur minimale des lignes (ListViewItem à 30px au lieu de 44px) et des marges intérieures (Padding à 2px au lieu de 8px) dans la liste des messages (MessageList) et la liste des événements du calendrier (EventList).
  - **Bouton à bascule dans le ruban** : ajout du bouton « Mode compact » (RibbonMailCompactDensityToggle et RibbonCalendarCompactDensityToggle) dans l'onglet *Affichage* du ruban Courrier et du ruban Calendrier.
  - **Intégration dans les Paramètres Windows 11 Fluent** : ajout d'une carte de réglage dédiée dans *Paramètres › Apparence* avec interrupteur à bascule synchronisé en direct.
  - **Persistance et mise à jour en direct** : mémorisation dans settings.json (layout.compact-density), application automatique au démarrage et rafraîchissement immédiat de la vue active lors du basculement.
- **Transférer en pièce jointe pour un message seul (Quickwin #2)** :
  - **Action dédiée « Transférer en PJ »** : ajout de boutons dédiés avec icône trombone dans le ruban Courrier (*Accueil*), dans la barre d'outils du volet de lecture (ReaderPane) et dans la fenêtre indépendante (MessageWindow), associés au raccourci clavier Ctrl+Maj+F.
  - **Distinction claire entre transfert dans le corps et transfert en PJ** : le bouton « Transférer » (Ctrl+F) effectue désormais un transfert classique dans le corps du texte (inline avec historique et citations TR:) pour un message unique, tandis que « Transférer en PJ » exporte le message au format standard .eml et le joint à un nouveau courriel.
  - **Menu contextuel enrichi** : présence systématique des actions « Transférer » (dans le corps) et « Transférer en pièce jointe (.eml) » par clic droit dans la liste des messages, que ce soit pour un courriel unique ou pour une sélection multiple.
  - **Prise en charge complète dans MessageWindow** : intégration de l'exportation EML via CoreBridge et ouverture de la fenêtre de rédaction ComposeWindow avec la pièce jointe attachée directement depuis une fenêtre de message individuelle.
- **Option « Volet de lecture : Désactivé » (Quickwin #1)** :
  - **Désactivation complète du volet de lecture** : accessible depuis le menu *Affichage › Volet de lecture* (options *À droite*, *En bas*, *Désactivé*), avec mémorisation dans `settings.json` (`layout.reading-pane`).
  - **Extension pleine largeur de la liste de messages** : lorsque le volet est désactivé, la liste s'étend sur toute la largeur disponible (colonnes 2 à 4) pour un confort de lecture optimal des listes denses.
  - **Ouverture native des messages** : maintien de la sélection active pour les actions du ruban (Répondre, Transférer, Supprimer, etc.) et ouverture des messages en fenêtre indépendante (`MessageWindow`) au double-clic ou avec la touche Entrée.
  - **Optimisation des performances de compilation** : régulation des processus concurrents de compilation (`-m:4` dans `tools\build-winui-app.bat`) évitant la saturation de pagination mémoire lors des builds parallèles.
- **Recherche complète dans les tâches (Ctrl+E et recherche locale)** :
  - **Recherche unifiée dans la barre de titre** : placeholder contextuel « Rechercher dans les tâches (Ctrl+E) » lors de l'affichage de la vue Tâches, avec masquage automatique du sélecteur de portée de dossiers de courriels.
  - **Champ de recherche dédié dans la vue Tâches (`TaskSearchBox`)** : situé dans l'en-tête de la vue, synchronisé en temps réel avec le champ de la barre de titre.
  - **Filtrage multicritère instantané** : recherche insensible à la casse dans l'objet, la description, les catégories, le nom de la liste et le compte source de chaque tâche, avec message informatif en cas d'absence de résultat et effacement rapide via la touche Échap.
- **Volet latéral droit des tâches (To-Do bar façon Outlook)** :
  - **Barre des tâches compacte toujours accessible** : disponible dans les vues Courrier, Calendrier et Contacts, permettant de garder ses tâches en vue sans quitter son travail en cours (repli automatique sur les vues Tâches et Paramètres).
  - **Tri optimisé pour la productivité** : classement prioritaire des tâches en retard en tête (mise en évidence en rouge), suivi des tâches du jour, des échéances les plus proches, et des tâches sans date, avec départage par niveau de priorité.
  - **Ligne de tâche compacte et interactive** : case à cocher pour achever la tâche en un clic, intitulé tronqué, échéance relative intelligente (« Aujourd'hui », « Demain », « En retard (X j) »), indicateur de priorité haute et compte de rattachement. Clic sur une tâche pour ouvrir l'éditeur complet.
  - **Ajout rapide de tâche au clavier** : champ de saisie directe en haut de volet permettant de créer instantanément une tâche par simple appui sur la touche Entrée.
  - **Gestion de la disposition et raccourci clavier** : affichage/masquage via le ruban *Affichage* (*Volet des tâches*) dans Courrier, Calendrier et Contacts, raccourci clavier universel `Alt+F2`, séparateur redimensionnable à la souris (`PaneSplitter`), et persistance automatique de la largeur et de l'état de visibilité dans `settings.json`.
- **Affichage du compte source dans la vue Tâches** :
  - **Badge de compte dédié pour chaque tâche** : affichage pour chaque tâche d'un badge Fluent avec icône, nom du compte source et adresse email associée (ou mention « Sur cet ordinateur » si compte local).
  - **Résolution hiérarchique du compte** : intégration dans `evo-core` (`task_item_new` et `core_list_sources`) des métadonnées `account_name`, `account_email` et `account_uid`, complétée par une résolution WinUI transversale (`GetTaskAccountInfo`) croisant `m_task_lists` et `m_accounts`.
  - **Affichage du compte dans l'éditeur de tâche** : indication contextuelle du compte et de son adresse email sous le choix de la liste de tâches.
- **Action « Créer une tâche » depuis un courriel ou un événement** :
  - **Création depuis un courriel** : accessible depuis le ruban Courrier (*Créer une tâche*, raccourci `Ctrl+Maj+T`), la barre d'outils du volet de lecture et le menu contextuel par clic droit sur un message. Préremplissage automatique du sujet, de la description structurée (expéditeur, date, objet et extrait du message), de l'échéance par défaut (J+1), de la priorité (Haute si le message est marqué d'un suivi, sinon Normale) et présélection de la liste de tâches du compte du message.
  - **Création depuis un événement du calendrier** : bouton dédié dans la fenêtre de détails de l'événement permettant de générer instantanément une tâche liée avec reprise du titre, du lieu, de la description et de l'échéance synchronisée avec la date de l'événement.
- **Indicateur d'avancement dans la barre des tâches (`ITaskbarList3`)** :
  reflète l'état de synchronisation en arrière-plan directement sur l'icône de
  l'application (progression indéterminée, pourcentage ou rouge en cas d'erreur).
- **Refonte complète de la page Paramètres (style Windows 11 Fluent)** :
  - **Navigation par catégories** : volet latéral dédié à gauche proposant 11 catégories structurées (Comptes, Courrier, Calendrier, Contacts, Tâches, Notifications, Apparence, Confidentialité et sécurité, Mise à jour, Avancé, À propos) avec icônes Fluent adaptatives.
  - **Panneau de cartes de réglages Windows 11 (`SettingsCard`)** : conteneur structuré à droite organisant chaque catégorie en sections thématiques et cartes visuelles épurées (titre, description sans jargon, icône, contrôle interactif aligné à droite).
  - **Fil d'Ariane cliquable** : navigation hiérarchique en haut de page (« Paramètres › [Catégorie] ») facilitant le repérage et le retour rapide.
  - **Recherche rapide en direct (`SettingsSearchBox`)** : barre de recherche intégrée avec auto-complétion instantanée filtrant parmi tous les réglages et ouvrant directement la catégorie concernée.
  - **Enregistrement immédiat dans `settings.json`** : persistance automatique et directe des réglages via `Settings::Instance()` dès modification d'un interrupteur ou d'une liste déroulante.
  - **Bascule dynamique du thème** : application immédiate des modes Clair, Sombre ou Système sur l'ensemble de l'interface (`RequestedTheme`) sans redémarrage.
  - **Raccourci clavier universel `Ctrl+,`** et navigation contextuelle directe vers n'importe quelle catégorie (`SelectSettingsCategory`).
  - **Gestion complète du Calendrier dans les Paramètres** : personnalisation des jours de travail ouvrés (boutons Lun-Dim), plage des heures de bureau (début/fin avec cadrage automatique du défilement de la grille), premier jour de la semaine (Lundi, Dimanche ou Samedi avec répercussion instantanée sur le mini-calendrier et les calculs de période), affichage des numéros de semaine, et sélection du fuseau horaire (principal et second fuseau horaire).
- **Écran de démarrage fluide (`SplashWindow`) et robustesse au démarrage** :
  affichage instantané d'une fenêtre de démarrage épurée au lancement de l'application
  pendant l'initialisation du moteur et des services EDS. Transition fluide vers
  la fenêtre principale dès l'événement `ready` ou dès qu'une action utilisateur
  (mot de passe, certificat TLS, OAuth2) est requise. Sécurisation complète contre
  les événements asynchrones précoces du moteur avant montage de l'interface XAML.
- **Support de Visual Studio 2026 et bascule multi-version** : support natif du
  compilateur MSVC 19.51 et de MSBuild 18.x (Visual Studio 2026 Community) dans
  l'ensemble des scripts de compilation (`tools\env.bat`, `tools\check-prereqs.ps1`,
  `tools\build-winui-app.bat`). Variable d'environnement `VS_VERSION` permettant de
  choisir facilement la version (`2026`, `2022`, ou détection automatique de la
  plus récente).
- **Fenêtre de message indépendante (`MessageWindow`)** : ouverture d'un
  message dans une fenêtre dédiée au double-clic, avec la touche Entrée ou via
  l'action *Ouvrir* du menu contextuel. Prise en charge des actions complètes
  (Répondre, Répondre à tous, Transférer, Supprimer, Déplacer, Lu / Non lu,
  Impression native WebView2, enregistrement des pièces jointes), navigation
  message précédent / suivant, mémorisation des dimensions et de la position,
  marquage comme lu à l'ouverture et reprise automatique des brouillons dans la
  fenêtre de rédaction (`ComposeWindow`).
- **Portée de recherche dans la barre de titre** : sélecteur compact intégré
  à la barre de titre permettant de choisir la portée de la recherche :
  *Dossier courant*, *Sous-dossiers*, *Compte courant* ou *Tous les comptes*.
  Texte d'aide dynamique, recherche récursive en profondeur dans le moteur
  `evo-core` (Camel), et affichage discret du dossier / compte d'origine
  sur chaque message trouvé lors d'une recherche transversale.
- **Démarrage rapide depuis le cache local et reprise de session** : affichage
  immédiat de l'arborescence des dossiers et des compteurs depuis le cache Camel
  local sans bloquer sur le réseau (`CAMEL_STORE_FOLDER_INFO_FAST`). Chargement
  instantané de la liste des messages depuis le résumé local (`CamelFolderSummary`)
  avec rafraîchissement asynchrone en arrière-plan. Restauration automatique
  du dernier compte et dossier consulté (`session.account` / `session.folder`).
- **Barre d'état permanente façon Outlook avec indicateur de progression** :
  barre d'état intégrée en bas de fenêtre affichant l'état du dossier à gauche
  (« Tous les dossiers sont à jour », nombre d'éléments), et une barre de
  progression permanente avec libellé indicatif des opérations réseau en cours
  (événements `"progress"` d'`evo-core`, pourcentage d'avancement et masquage
  automatique après achèvement).
- **Indicateur d'état des connexions et mode hors connexion** :
  pastille d'état dynamique (vert connecté, orange en cours, rouge erreur, gris
  hors connexion) avec menu déroulant détaillant chaque compte. Bouton *Se
  reconnecter* pour relancer un compte en échec sans passer par les Paramètres,
  bouton de réessai global, et bascule manuelle *Travailler hors connexion*
  (`evo_core_set_offline`).
- **File d'attente et Boîte d'envoi (« Outbox »)** : les courriels rédigés et
  envoyés en mode hors connexion (ou lors d'une indisponibilité réseau imprévue)
  sont automatiquement stockés dans le dossier local spécial *Boîte d'envoi*
  (`"local"`, `"Outbox"`). Dès que l'application repasse en ligne, ou lors
  d'une relève (F9 ou vérification manuelle), les messages en attente sont
  automatiquement transmis via leurs serveurs SMTP respectifs, copiés dans
  *Éléments envoyés*, puis purgés de la Boîte d'envoi.
- **Simplification de l'assistant d'ajout de compte** : interface moderne et
  épurée présentant uniquement l'adresse, le nom et le mot de passe (ou le flux
  moderne). Les détails techniques (hôtes, ports, nom d'utilisateur, chiffrement
  TLS/STARTTLS) sont masqués sous un `Expander` replié par défaut, avec vérification
  préalable de connectivité avant création effective de la source EDS.
- **Boîte de dialogue complète de modification de compte** : accessible depuis
  les paramètres (*Modifier*) et par clic droit sur l'arborescence (*Propriétés du
  compte…*). Permet de modifier le nom, l'expéditeur, l'adresse, les serveurs entrants
  et sortants, le mot de passe avec test de connectivité, ainsi que d'assigner
  les rôles de dossiers spéciaux (`CAMEL_FOLDER_TYPE_*` : Envoyés, Brouillons,
  Corbeille, Courrier indésirable, Archives) à partir des dossiers du compte.
- **Connexion Google / Gmail OAuth2 conforme RFC 8252** : authentification Google
  exécutée dans le navigateur par défaut avec redirection sur écouteur local
  loopback (`http://127.0.0.1:<port>`), éliminant l'erreur `disallowed_useragent`
  de WebView2. Prise en charge explicite du repli par « mot de passe d'application »
  avec lien d'aide Google.
- **Validation des portées OAuth2 M365 et Gmail** : intégration des portées
  étendues pour la messagerie, les calendriers et les contacts en ligne.

## [0.9.5] — 2026-11-02

### Ajouté

- **Recherche de mise à jour** dans *À propos* : la version installée est
  comparée à la dernière version publiée sur GitHub (Releases) ; si une
  version plus récente existe, un lien mène à sa page de téléchargement.
  Une notification discrète au démarrage prévient également lorsqu'une
  nouvelle version est disponible.
  Aucune donnée n'est envoyée, seule la page publique de la dernière
  version est lue.

### Modifié

- Code : `MainWindow.xaml.cpp` découpé en un fichier par domaine
  (`MainWindow.<Domaine>.cpp`) ; compilation limitée à 4 processus
  parallèles (chaque `cl.exe` charge le PCH d'environ 1 Go).

## [0.9.4] — 2026-11-01

### Ajouté

- **Arborescence des dossiers** : dossiers spéciaux en premier, dans l'ordre
  d'Outlook (Boîte de réception, Brouillons, Éléments envoyés, Éléments
  supprimés, Archive, Courrier indésirable, Boîte d'envoi), puis les autres
  par ordre alphabétique ; noms des dossiers spéciaux en français quel que
  soit le serveur ; dossiers spéciaux reconnus aussi d'après la
  configuration du compte (serveurs IMAP sans SPECIAL-USE) ; icône au trait
  pour la Boîte de réception.
- **Notifications confidentielles** : « Nouveau message » seulement, sans
  expéditeur ni objet (présentation, partage d'écran) ; bouton du ruban
  *Affichage*, commande de la barre d'accès rapide et interrupteur dans
  *Paramètres › Confidentialité*.
- **Position du volet de lecture** (*Affichage › Volet de lecture*) : à
  droite ou en bas de la liste des messages, choix mémorisé ; *Rétablir la
  disposition* le remet à droite.
- **Archiver** (ruban, volet de lecture, menu contextuel, barre d'accès
  rapide, touche Retour arrière) : vers le dossier Archive du compte, créé
  au premier usage ; **Copier vers** un dossier ; derniers dossiers
  utilisés en tête des menus *Déplacer* / *Copier vers* ; menu *Nouvel
  élément* : Rendez-vous (Réunion à venir).
- **Recherche dans la barre de titre** (Ctrl+E, Échap pour effacer) : dossier
  courant en Courrier, contacts en Contacts ; barre de titre plus haute.
- **Calendrier** : barre de navigation (Aujourd'hui, précédent / suivant,
  plage affichée, période Jour / Semaine / Mois mémorisée) ; volet
  *Calendriers* par compte avec couleur, case à cocher pour afficher ou
  masquer chaque calendrier (seuls les calendriers affichés sont lus) et
  calendrier actif des nouveaux événements ; pastille de couleur du
  calendrier devant chaque événement.
- **Ouvrir un événement** (clic dans la liste) : titre, lieu, début et fin
  (date et heure, journée entière), description modifiables et
  enregistrés sur le serveur, organisateur et participants avec leur
  réponse, suppression ; lecture seule pour un agenda non modifiable.
  Nouvel événement : date de fin et journée entière.
- **Grille du calendrier**, comme Outlook : vues Jour, Semaine de travail
  et Semaine (une colonne par jour, blocs colorés par calendrier, côte à
  côte s'ils se chevauchent, journée entière en haut, ligne de l'heure
  actuelle), vue Mois (six semaines, « +N autres ») et vue Liste ; un clic
  ouvre l'événement. Événements périodiques développés en occurrences.
- **Compte Gmail / Google Workspace** complet : collection Google (courrier
  IMAP / SMTP de Gmail, agendas et contacts découverts par EDS), une seule
  connexion Google sans Client ID à saisir ; jetons du courrier fournis et
  renouvelés par le service OAuth2 Google d'EDS. Agendas Google découverts
  après la connexion : le moteur répond aux demandes d'identification du
  service des comptes (relance de l'authentification avec le jeton OAuth2,
  ou nouvelle connexion si le jeton manque).
- **Contacts** : volet *Carnets d'adresses* par compte (Microsoft 365,
  Gmail, local) avec case à cocher pour afficher ou masquer chaque carnet
  (seuls les carnets affichés sont lus) et carnet actif des nouveaux
  contacts ; carnet qui ne s'ouvre pas signalé dans le journal.
- **Autocomplétion des destinataires** (À, Cc, Cci) dès 2 caractères :
  tous les carnets d'adresses, annuaire de l'organisation (GAL) compris,
  avec le carnet d'origine ; adresses récemment utilisées en tête ;
  flèches et Entrée pour choisir.
- Journaux des services evolution-data-server (`evolution-source-registry`,
  fabriques carnet d'adresses / agenda) dans
  `%LOCALAPPDATA%\EvolutionWin\<service>.log`, pour le diagnostic.

### Corrigé

- Calendrier vide : le service des agendas s'arrêtait en lisant les
  calendriers (libération d'une ressource jamais obtenue sous Windows dans
  Camel) ; avertissement dans le journal pour un calendrier qui ne s'ouvre
  pas.
- Comptes Microsoft 365 / Exchange sur une installation par le MSI : les
  services evolution-data-server cherchaient leurs modules (registre,
  carnets d'adresses, agendas) dans le dossier de compilation au lieu de
  `C:\Program Files\Evolution` ; le nouveau compte n'était jamais
  enregistré (« Compte introuvable »).
- Dossiers d'un compte Microsoft 365 ou Exchange qui disparaissaient après
  la première relève : sous Windows, l'enregistrement de la liste des
  dossiers était pris pour une suppression du fichier, et la liste était
  vidée.
- Ajout d'un compte : attente de son enregistrement par le service des
  comptes (jusqu'à 30 s) avant de le connecter, message d'erreur clair
  sinon ; plus de compte sans nom dans la liste.
- Ajout d'un compte Microsoft 365 professionnel : la connexion échouait
  (« CredWrite: Le relais a reçu des données incorrectes ») car les jetons
  OAuth2 dépassaient la taille maximale d'un identifiant Windows (2 560
  octets) ; les secrets longs sont désormais répartis sur plusieurs
  identifiants du Gestionnaire d'identifiants.
- Une erreur inattendue dans un rappel du moteur ou un gestionnaire
  d'événement n'arrête plus l'application : elle est consignée dans
  `%LOCALAPPDATA%\EvolutionWin\errors.log` (plantage à l'ajout d'un compte
  Microsoft 365 sur une installation neuve).
- `publish-release.bat` : les titres du CHANGELOG (« ### Ajouté »…) sont
  conservés dans le message de l'étiquette Git.

## [0.9.3] — 2026-10-31

### Ajouté

- **Releases GitHub** : chaque version est publiée dans les Releases du
  dépôt (installeur MSI, empreinte `.sha256`, notes de ce journal) par
  `tools\publish-release.bat` ; `dist/` n'est plus versionné.
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
- **Barre latérale** : barre d'icônes fixe entre Courrier, Calendrier,
  Contacts et Tâches (sans bouton de dépliage, noms en info-bulle) ; badge des
  messages non lus des boîtes de réception sur Courrier.
- **Bandeau d'information** dans les vues Courrier et Calendrier : avis
  avec bouton d'action, par exemple un compte en attente de connexion
  (fenêtre de mot de passe ou de connexion Microsoft fermée) avec *Saisir le
  mot de passe* / *Se connecter*.
- **Ruban** des vues Courrier et Calendrier (onglets *Fichier*, *Accueil*,
  *Envoi/réception*, *Dossier*, *Affichage*) : enregistrer, imprimer,
  paramètres, quitter ; nouveaux éléments, répondre, transférer, lu / non
  lu, suivi ; relève F5 / F9 / Maj+F9 ; gestion du dossier affiché ;
  filtres, volet des dossiers masquable et disposition par défaut ;
  Calendrier : nouvel événement, exporter, aujourd'hui / 30 prochains jours.
- **Barre d'accès rapide** dans la barre de titre : Envoyer/Recevoir et
  Nouveau message par défaut ; personnalisable (tous les dossiers, répondre,
  supprimer, imprimer, nouvel événement, nouvelle tâche), choix mémorisé.
- **Tri des messages** : ligne « Trier par » au-dessus de la liste et menu
  *Affichage › Trier par* ; date, expéditeur, destinataire, objet (sans
  « Re: » / « TR: »), taille, pièces jointes, indicateur, lu / non lu,
  catégories ; ordre inversable, tri mémorisé pour chaque dossier.
- **Ajout de compte unifié** : bouton *Ajouter un compte* ; à partir de
  l'adresse, détection du type (Microsoft 365 / Outlook.com, Exchange de
  l'entreprise, Gmail, IMAP / SMTP) et des serveurs : fournisseurs connus
  (base ISPDB de Thunderbird embarquée, MPL 2.0), serveurs MX du domaine,
  Autodiscover, autoconfig du domaine et ISPDB en ligne, enregistrements
  SRV ; paramètres proposés à confirmer ou corriger, type modifiable,
  configuration manuelle possible. Nouveau type de compte *Exchange de
  l'entreprise* (EWS, mot de passe).

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
