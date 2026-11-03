# Quickwins proposés — Evolution Windows

Propositions d'implémentations facilement réalisables, issues de la lecture du [TODO.md](TODO.md) : tâches 🟢 encore ouvertes, plus quelques 🟡 qui s'appuient sur du code déjà présent (EDS, `evo-core`, WinUI). Estimations : 🟢 ≈ quelques heures, 🟡 léger ≈ 1 à 2 jours.

---

## 🎯 Top 5 recommandé (meilleur rapport effet / coût)

| # | Tâche | Réf. TODO | Pourquoi c'est rapide |
|---|-------|-----------|-----------------------|
| 1 | **Calendrier par URL ICS / webcal** | §14 — _À partir d'Internet_ | Le backend EDS `webcal` existe déjà : il suffit d'un champ URL + création de la source. |
| 2 | **Sélection et couleurs individuelles des calendriers** (persistance des cases cochées) | §14 | Cases et couleurs déjà en place ; il reste à mémoriser l'état dans les préférences. |
| 3 | **Choix mémorisé du mode « conversations » par dossier** | §5 | Simple clé de préférence par dossier (indépendante du moteur de regroupement). |
| 4 | **Marquer / démarquer comme indésirable** | §8 | Déplacement vers / depuis _Indésirables_ avec la fonction de déplacement existante. |
| 5 | **Export des messages au format EML** (dossier courant ou sélection) | §18 | L'export `.eml` unitaire existe dans `evo-core` : boucle + `FolderPicker`. |

---

## 1. Calendrier

- [x] **Ajout d'un calendrier depuis une URL ICS / webcal** (§14) — fait (`evo_core_create_calendar`, bouton « + » du volet des calendriers)
  - Entrée de menu _Ajouter un calendrier › À partir d'Internet_ ; `ContentDialog` avec URL, nom, couleur.
  - Création d'un `ESource` de backend `webcal`, actualisation périodique.
- [x] **Nouveau calendrier vide** (§14) — fait (local ; même boîte de dialogue, adresse laissée vide)
  - Création d'un `ESource` local (ou sur le compte) avec nom et couleur.
- [x] **Mémoriser les calendriers cochés et leur couleur** entre les sessions (§14) — déjà en place (préférence `calendar.visible`)
  - Clé de préférence par UID de source ; restauration au démarrage de la vue.
- [ ] **Bascule superposé / côte à côte** : mémoriser le mode choisi (§12)
- [ ] **Bloc « temps de travail individuel »** (§13)
  - Raccourci du bouton _Nouvel événement_ : événement sans participants, `TRANSP:OPAQUE` (occupé), titre « Focus » prérempli.

## 2. Courrier

- [x] **Mémoriser par dossier l'affichage en conversations** (§5) — fait
  - Dictionnaire `uid dossier → bool` (`conversations.<compte>/<dossier>`) dans les préférences, lu à la sélection du dossier et bouton à bascule dans le ruban Affichage.
- [x] **Marquer / démarquer comme indésirable** (§8) — fait
  - Déplacement vers / depuis le dossier _Indésirables_ avec la fonction de déplacement existante ; entrée dans le menu contextuel et le ruban.
- [ ] **Autosave du brouillon** (§7)
  - Minuteur (ex. 60 s) dans `MainWindow.Compose.cpp` qui réutilise l'enregistrement de brouillon existant.
- [x] **Importance (haute / basse)** : affichage d'une icône dans la liste à partir de l'en-tête `Importance` / `X-Priority` (§7) — fait
  - Choix à la rédaction : ajout des boutons d'importance haute / basse dans `ComposeWindow` et transmission dans le payload d'envoi.
- [ ] **Afficher les en-têtes bruts / source du message** (§7)
  - `ContentDialog` avec texte sélectionnable + bouton _Copier_, à partir de la source MIME déjà accessible pour l'export `.eml`.

## 3. Interface et paramètres

- [x] ~~**Thème clair / sombre / système** (§19)~~ — déjà fait
- [x] **Taille du texte** (§19) — fait pour la lecture (liste Paramètres › Apparence appliquée comme zoom du lecteur)
  - Échelle appliquée via une ressource XAML globale.
- [ ] **Compte activé / désactivé sans suppression** (§3 — Synchronisation)
  - Bascule de `ESourceExtension` _enabled_ dans la boîte de modification du compte.
- [ ] **Quota du compte** dans l'onglet _Fichier_ (§3)
  - `camel_folder_get_quota_info_sync` + `ProgressBar` ; masqué si non supporté.
- [ ] **Dates, heures et tailles localisées** (§20)
  - Remplacer les formats codés en dur par `Windows.Globalization.DateTimeFormatting`.

## 4. Installation et maintenance

- [ ] **Nettoyage des fichiers temporaires du glisser-déposer** (§18)
  - Suppression à la fin du glisser ou à la fermeture (dossier temporaire dédié, purge au démarrage).
- [ ] **`MSIRESTARTMANAGERCONTROL=Disable`** dans le MSI (§1)
  - Une propriété à ajouter dans le WiX ; supprime toute boîte « fichiers en cours d'utilisation ».
- [ ] **Relance de l'application après installation silencieuse** (`LAUNCHAPP=1`) (§1)
  - Action personnalisée WiX conditionnelle, lancée dans la session de l'utilisateur.
- [ ] **Journalisation des processus fermés** dans le journal MSI (§1)
  - Ajout de lignes via `MsiProcessMessage` ou mention dans l'action de fermeture.

## 5. Petits plus 🟡 « presque gratuits »

- [ ] **Bouton _Annuler_ sur la barre de progression** (§4) : `GCancellable` déjà passé aux appels de relève, il manque l'exposition dans l'interface.
- [ ] **Réexécuter les règles / filtres** : hors périmètre tant que l'éditeur n'existe pas ; à laisser de côté.
- [ ] **Recherche dans le calendrier** (§12) : filtre local sur titre, lieu, description, à partir des événements déjà chargés.

---

## Ordre de réalisation suggéré

1. Calendrier par URL ICS + persistance des calendriers cochés (visible en démo).
2. Marquer comme indésirable + importance dans la liste.
3. Taille du texte.
4. Export EML + fichiers temporaires.
5. Réglages MSI (`MSIRESTARTMANAGERCONTROL`, `LAUNCHAPP`).
