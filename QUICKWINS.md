# Quickwins prioritaires — Evolution Windows

Ce document récapitule les tâches à fort impact et faible complexité technique (**difficulté 🟢**, réalisables en quelques heures à 1 jour) identifiées dans le [TODO.md](TODO.md).

Ces chantiers ne dépendent pas de refontes architecturales complexes et apportent un gain immédiat en termes d’ergonomie, de parité avec Outlook et de confort utilisateur sous Windows 11.

---

## 🎯 Top 3 des Quickwins immédiats

| # | Tâche | Référence TODO | Impact & Existant |
|---|---|---|---|
| 1 | **Option « Volet de lecture : Désactivé »** | [§2 Disposition des panneaux](TODO.md#demo-volet-lecture) | L'option est actuellement grisée. La fenêtre indépendante par double-clic étant déjà fonctionnelle, il suffit de permettre de masquer le volet et d'ouvrir les messages dans leur propre fenêtre. |
| 2 | **Transférer en pièce jointe pour un message seul** | [§7 Actions sur les messages](TODO.md) | Fonctionnalité déjà codée pour la sélection multiple d'emails. Il s'agit simplement d'exposer l'action lorsqu'un seul message est sélectionné. |
| 3 | **Mode compact (espacement plus étroit des listes)** | [§2 Disposition des panneaux](TODO.md) | Ajustement XAML pur sur les styles et paddings des `ListViewItem` (Mail et Calendrier) pour afficher plus d'éléments à l'écran. |

---

## 1. Ergonomie et affichage (XAML / WinUI 3)

- [x] **Volet de lecture désactivé** (menu *Affichage › Volet de lecture*)
  - Activer l'entrée actuellement grisée dans le menu.
  - Masquer le volet de lecture principal.
  - Ouvrir directement le message en fenêtre indépendante lors de la sélection / double-clic.
  - Mémoriser le choix dans les préférences locales.
- [x] **Mode compact des listes** (Mail et Calendrier)
  - Bascule dans le ruban ou les paramètres d'affichage.
  - Réduction de la hauteur des lignes et des marges intérieures.
- [ ] **Volet des dossiers réductible** (Mail et Calendrier)
  - États : normal, réduit (icônes seules / mini-calendrier) ou masqué.
  - Libère de l'espace sur les écrans plus compacts.
- [ ] **Ordre des comptes dans le volet des dossiers**
  - Placer par défaut le compte principal en haut de la liste.
  - Proposer une option pour trier les dossiers de A à Z ou dossiers spéciaux d'abord.

---

## 2. Actions et confort sur les messages

- [x] **Transférer en pièce jointe (message unique)**
  - Ajouter l'option dans le menu contextuel et le ruban d'action d'un message sélectionné.
  - Réutiliser la routine existante de génération de fichier `.eml` en pièce jointe.
- [ ] **Aperçus sécurisés de pièces jointes simples**
  - **Images** (PNG, JPEG, GIF, BMP, WebP) : affichage direct dans le volet de lecture via `BitmapImage`.
  - **Fichiers texte, JSON, CSV, logs** : affichage direct en texte brut non interprété dans un conteneur dédié.
- [x] **Bouton « Afficher dans le navigateur »**
  - Permettre d'ouvrir le courriel ou le portail webmail correspondant dans le navigateur système par défaut.

---

## 3. Intégration Windows 11

- [x] **Progression et badge sur la barre des tâches** (`ITaskbarList3`)
  - Jauge de progression verte lors des synchronisations / relèves de boîte.
  - Jauge rouge en cas d'erreur de connexion ou d'échec d'envoi.
  - Badge indiquant le nombre de messages non lus.
- [ ] **Lecture à voix haute du message**
  - Utilisation de l'API native `Windows.Media.SpeechSynthesis`.
  - Bouton dédié dans la barre d'outils du volet de lecture et de la fenêtre indépendante.

---

## 4. Navigation et Calendrier

- [x] **Raccourcis de navigation globale**
  - `Ctrl+Alt+1` : Courrier
  - `Ctrl+Alt+2` : Calendrier
  - `Ctrl+Alt+3` : Contacts
  - `Ctrl+Alt+4` : Tâches
- [x] **Atteindre une date** (`Ctrl+G`)
  - Boîte de dialogue simple pour naviguer vers une date choisie.
  - Bouton rapide « Aujourd’hui ».
- [ ] **Échelle de temps du calendrier**
  - Choix du pas horaire dans la grille Jour / Semaine : 15, 30 ou 60 minutes.
- [ ] **Recherche rapide de contacts**
  - Champ de saisie direct dans le ruban avec filtrage dynamique dans le carnet d'adresses.

---

## 5. Options utilisateur et paramètres de base ([§19](TODO.md))

- [x] **Gestion du statut « Lu / Non lu »**
  - Paramètre pour marquer automatiquement comme lu : immédiatement, après N secondes d'affichage, ou uniquement lors d'un changement de sélection.
- [x] **Vider la Corbeille à la fermeture**
  - Option dans les paramètres pour purger automatiquement les éléments supprimés à l'arrêt de l'application.
- [x] **Confirmations de sécurité**
  - Confirmation avant suppression définitive (`Maj+Suppr`).
  - Avertissement si un message mentionne une pièce jointe mais qu'aucun fichier n'est joint.
- [x] **Module actif au démarrage**
  - Permettre de choisir d'ouvrir par défaut le Calendrier plutôt que le Courrier au lancement.
