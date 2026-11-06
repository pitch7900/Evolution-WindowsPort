# Points à valider — version 1.0.0

Liste des fonctions livrées en 1.0.0 qui ont été codées et testées par les tests unitaires, mais **pas (ou pas entièrement) validées à la main** sur un compte réel. Les corrections issues de ces validations feront l'objet de versions mineures (1.0.x).

Légende : `[ ]` à valider · `[x]` validé · `[!]` essayé, défaut constaté (voir la note).
Pour chaque point : compte nécessaire entre parenthèses. « Test » = compte de test (IMAP, jetable) ; « M365 » = Exchange Online / Microsoft 365 ; « Gmail » = compte Google.

> Mise à jour : cocher la case, ajouter la date et une note courte. Un défaut devient une ligne dans `TODO.md` (section concernée) et une entrée du `CHANGELOG.md` de la version corrective.

---

## 1. Courrier

- [x] Aperçu des pièces jointes : PDF, image, texte (Test) — validé.
- [x] Glisser-déposer d'une pièce jointe vers le Bureau (Test) — validé.
- [x] Suivi des messages : drapeau, échéance, terminé, effacer (Test) — validé.
- [x] Aperçu d'une pièce jointe HTML (WebView2 isolé, scripts désactivés) (Test).
- [x] Accusé de lecture : bandeau « Envoyer l'accusé / Ignorer », envoi, mémorisation (Test) — validé.
- [x] Réglage des accusés de lecture Demander / Jamais / Toujours (Paramètres › Confidentialité et sécurité) : présence et conservation après redémarrage.
- [x] Accusé de lecture hors-ligne : message d'erreur au lieu d'un envoi (Test).
- [x] Boîte de réception unifiée et dossiers virtuels globaux (Envoyés, Brouillons, Supprimés de tous les comptes) : mélange par date, compte d'origine par ligne, actions (lire, marquer, déplacer, supprimer, répondre) sur le bon compte (au moins deux comptes distants).
- [x] Messages d'état compacts dans la barre du bas : lisibilité, durée d'affichage (7 s / 12 s / 20 s), bouton de fermeture.

## 2. Invitations et réunions

- [x] Invitation reçue : bandeau Accepter / Provisoire / Refuser, mise à jour de l'agenda, réponse envoyée à l'organisateur (M365 vers Test) — validé.
- [x] Invitation annulée : bandeau orange et « Retirer de l'agenda » — validé.
- [x] Invitation reçue : réponse d'un invité visible chez l'organisateur (« X a accepté la réunion »).
- [x] Création d'un événement avec participants : choix du compte expéditeur et envoi des invitations pour un agenda local, arrivée chez un destinataire Gmail (agenda « Sur cet ordinateur »).
- [x] Création d'un événement avec participants sur un agenda Exchange Online / Google : le serveur envoie les invitations (pas de doublon envoyé par l'application).
- [x] Création d'un événement : liste « Calendrier » (calendrier par défaut pré-sélectionné, pas de calendrier de personne) ; un calendrier en lecture seule (partagé) refuse l'écriture avec un message clair puis disparaît de la liste (retour d'un test : « RemoteCalendarAccessDenied » en créant une invitation vers Gmail depuis Exchange Online, à refaire).
- [x] Événement privé : case « Privé » à la création et à la modification ; contrôle dans Outlook Online (cadenas, détails masqués pour un autre utilisateur).
- [x] Réunion en ligne Microsoft Teams : case dans la fenêtre d'un nouvel événement (M365) ; lien de visio, identifiant de réunion et numéros d'appel présents dans l'invitation.
- [ ] Réunion Google Meet : case dans la fenêtre d'un nouvel événement d'un calendrier Gmail (icône Meet, exclusive avec Teams) ; lien `meet.google.com` présent dans l'événement et dans l'invitation reçue.
- [x] Boîte partagée Microsoft 365 : clic droit sur le compte › « Ouvrir la boîte d'un autre utilisateur… », connexion Microsoft, dossiers de la boîte partagée, envoi avec l'adresse de la boîte partagée (M365 + boîte partagée avec droit FullAccess).
- [ ] Boîte partagée : agenda et contacts partagés (cases à l'ouverture), « Fermer la boîte partagée » (clic droit), choix de la boîte dans « De : » à la rédaction (envoi avec l'adresse de la boîte partagée, copie dans ses éléments envoyés).
- [x] Création d'un événement par sélection dans le calendrier : glisser (Jour / Semaine), clic simple (1 h), clic dans une case du mois.

## 3. Annuaire de l'organisation

- [x] Lien « Annuaire de l'organisation » sous chaque compte, recherche, fiche en lecture seule, écrire un message (M365, Google) — validé.
- [x] Adresse postale et numéros de téléphone dans la fiche — validé.
- [!] Photo du profil : Microsoft répond « aucune image » pour les comptes testés ; seules les initiales s'affichent (voir `TODO.md`, section Annuaire).
- [x] Suggestions « Annuaire » dans la rédaction (À:) — validé.

## 4. Réponses automatiques (Out of Office)

- [x] Exchange Online : lecture des réglages du serveur (état, dates, texte interne, texte externe) affichés **lisibles** dans les deux onglets (« Au sein » / « En dehors »).
- [x] Exchange Online : modification du texte, de la période et du public externe ; enregistrement ; contrôle dans Outlook Online.
- [x] Exchange Online : date de fin avant la date de début refusée, fenêtre qui reste ouverte.
- [x] Bandeau « Réponses automatiques activées » et bouton « Désactiver » (désactivation confirmée côté serveur).
- [x] Gmail : reconnexion du compte (écran « Google n'a pas validé cette application » : continuer via les paramètres avancés), accès aux réglages Gmail accordé ; lecture, activation et désactivation.
- [x] Gmail : la réponse automatique est reçue par un expéditeur extérieur.
- [x] Compte IMAP : message clair « type de compte non pris en charge » — validé.
- [x] Éditeur HTML des messages (non fait : texte brut seulement) — voir `TODO.md`.

## 5. Calendrier

- [x] Calendriers Exchange Online affichés automatiquement, groupes pliables sous le compte, case de groupe — validé (premiers retours).
- [x] Groupes d'Outlook lus par EWS (Salles, Team Infra N2 / N3, Mes calendriers, Calendriers des contacts) : bouton « Importer mes groupes Outlook » (autorisation Microsoft la première fois), contenu des groupes, synchronisation automatique à l'ouverture avec l'anneau de progression.
- [x] Calendriers de personnes et d'équipes (« Personnes et équipes… ») : mon équipe, recherche, rangement par groupe, lecture seule, message clair si le calendrier n'est pas partagé.
- [x] Catégories unifiées : palette (Paramètres › Catégories), affectation sur courriel / tâche / contact / événement, pastilles, persistance.
- [x] Filtres par catégorie dans les quatre listes (courriels, tâches, contacts, événements).
- [x] Colonne des calendriers redimensionnable (séparateur, largeur mémorisée, double-clic : automatique).
- [x] Barre de commandes du calendrier sur deux lignes sans chevauchement quand la largeur ne suffit pas — validé.

## 6. Comptes et installation

- [x] Microsoft 365 : validation de bout en bout de la collection `microsoft365` (calendriers et contacts) sur un tenant réel — voir `ROADMAP.md`.
- [x] Installation du MSI 1.0.0 sur un poste propre, première exécution, désinstallation (voir « Installation » ci-dessous).
- [ ] Mise à jour automatique depuis une version précédente (téléchargement, contrôle d'intégrité SHA-256 et signature).

## 7. Interface

- [x] Démarrage de l'application sans plantage après une mise à jour : si l'application plante au lancement après un changement d'interface, supprimer `build\intermediate\winui-app-obj` et relancer `tools\build-winui-app.bat` (build incrémental corrompu, voir `LLM.md`).
- [ ] Langue : textes de la fenêtre des réponses automatiques et de l'annuaire en anglais (les autres écrans restent en français : traduction complète prévue en 1.3.0).
- [x] Thème clair et thème sombre : lisibilité des nouvelles fenêtres (réponses automatiques, personnes et équipes, boîte d'autorisation Microsoft).

---

## Installation (MSI 1.0.0)

1. Désinstaller toute version précédente d'EvolutionWin.
2. Lancer `dist\Evolution-1.0.0-x64.msi` (double-clic ou `msiexec /i`), accepter les défauts.
3. Lancer EvolutionWin depuis le menu Démarrer : l'application s'ouvre, le compte de test se synchronise.
4. Désinstaller depuis Paramètres Windows › Applications ; vérifier qu'aucun processus `evolution-*.exe` ne reste actif.
