# Points à valider

Tests manuels de la version 1.3.0 (le code est livré et la suite `tools\run-tests.bat` est à 100 %, mais rien de ce qui suit n'a été essayé à la main). Cocher chaque point une fois vérifié.

## 1. Lecteur immersif

- [ ] Courrier › sélectionner un message › onglet Affichage › « Lecteur immersif » : plein écran, texte seul, centré.
- [ ] A−, A+, espacement et thème (clair, sépia, sombre) modifient l'affichage.
- [ ] Après un clic dans le texte, les touches + - T L ont le même effet.
- [ ] Échap (y compris après un clic dans le texte) ferme la fenêtre ; la croix aussi.
- [ ] Rouvrir le lecteur : taille, espacement et thème sont conservés.
- [ ] Message en HTML seul, puis message sans corps : affichage correct.

## 2. Liens `mailto:`

- [ ] Paramètres › Courrier › « Liens de messagerie (mailto:) » : l'état indique « non enregistré ».
- [ ] « Enregistrer… » : l'état passe à « enregistré » et les « Applications par défaut » de Windows s'ouvrent ; choisir EvolutionWin pour MAILTO.
- [ ] « Retirer l'enregistrement » : retour à « non enregistré », l'entrée disparaît de Windows.
- [ ] EvolutionWin fermé, lancer `build\EvolutionWin.exe "mailto:a@b.fr,c@d.fr?cc=e@f.fr&bcc=g@h.fr&subject=Caf%C3%A9&body=L1%0D%0AL2"` : une rédaction s'ouvre avec À, Cc, Cci, objet « Café » et un corps sur deux lignes.
- [ ] EvolutionWin déjà ouvert, même commande : nouvelle rédaction dans l'instance existante, pas de second EvolutionWin dans le Gestionnaire des tâches.
- [ ] Clic sur un lien `mailto:` depuis un navigateur ou un document (EvolutionWin par défaut) : même comportement.
- [ ] Option d'installation `REGISTERMAILTO=1` : voir section 5.

## 3. Exportation

- [ ] Fichier › « Exporter des données… », Courrier, compte réel, 2 dossiers dont un avec sous-dossiers, mbox, dossier cible vide : un `<dossier>.mbox` par dossier, arborescence reprise, progression dans la barre d'état, rapport de fin, bouton « Ouvrir le dossier ».
- [ ] Ouvrir un des `.mbox` produits avec Thunderbird.
- [ ] Même chose en EML avec « Limiter à une période » : seuls les messages de la période sortent, un `.eml` par message.
- [ ] Annulation : export d'un gros dossier IMAP, puis annuler depuis la barre d'état : le rapport indique « annulée ».
- [ ] Export des contacts et des agendas depuis la même fenêtre (assistants existants).
- [ ] Glisser un événement (vue Semaine puis pastille de la vue Mois, dont un événement récurrent) sur le Bureau : un `<titre>.ics` apparaît, titre, horaires, fuseau et récurrence corrects à l'ouverture dans Outlook ou Thunderbird.
- [ ] Glisser un contact (hors annuaire GAL) sur le Bureau : un `<nom>.vcf` apparaît et s'importe dans une autre application.
- [ ] Non-régression : double-clic sur un événement l'ouvre toujours, le clic droit garde son menu, un clic sur un contact affiche sa fiche.

## 4. Importation

- [ ] « Importer des données… » (menu Fichier, aussi dans les rubans Contacts, Calendrier, Tâches) › mbox ou `.eml`, dossier « Importé » : messages lus, date d'origine conservée.
- [ ] « Courrier : profil Thunderbird » : profils détectés listés ; dossiers et sous-dossiers recréés, non-lus conservés.
- [ ] « Courrier : dossier » sur un Maildir.
- [ ] Import d'un `.vcf` multi-contacts, puis d'un `.ics` dans un agenda ; un second import du même fichier ignore tout (pas de doublons).
- [ ] « Outlook (.pst/.ost) » : le texte explicatif (conversion avec `readpst`) s'affiche, « Importer » est grisé. _(`.pst`/`.ost` non pris en charge : aucune bibliothèque libre disponible)_
- [ ] Import dans un compte IMAP réel (pas seulement le compte local).
- [ ] Dossier local : sur « Sur cet ordinateur », créer un dossier, le supprimer, puis en recréer un du même nom : doit réussir (bug corrigé en 1.3.0).
- [ ] Recréer un dossier local supprimé avant le correctif (par exemple « Importé ») : doit réussir.

## 5. MSI : installation silencieuse (VM Windows 11 propre, session administrateur)

Le MSI se construit avec `tools\build-msi.ps1`.

- [ ] Installation : `msiexec /i dist\Evolution-1.3.0-x64.msi /qn /l*v i.log` : code 0, application dans `C:\Program Files\Evolution`, `CloseDependencies` présent dans `i.log`.
- [ ] Mise à jour pendant que l'application tourne (avec un compte, donc services EDS et WebView2 actifs) : réinstaller en `/qn`, pas de boîte « fichiers en cours d'utilisation », code 0 ou 3010.
- [ ] Après cette mise à jour, aucun `camel-index-control`, `gspawn-win64-helper`, `evolution-*` ni `msedgewebview2` d'EvolutionWin ne survit ; un `msedgewebview2` d'Edge ou de Teams n'a pas été tué.
- [ ] Multi-sessions : avec un autre utilisateur (ou une session déconnectée) qui fait tourner EvolutionWin, refaire la mise à jour : ses instances se ferment.
- [ ] `FORCECLOSE=0` : aucune fermeture, code 3010 ou remplacement au redémarrage.
- [ ] Désinstallation sans purge (`msiexec /x … /qn`) : `%LOCALAPPDATA%\EvolutionWin` et `%LOCALAPPDATA%\evolution` restent.
- [ ] Désinstallation avec `PURGEUSERDATA=1` : les 3 dossiers de données et les identifiants (`cmdkey /list | findstr Evolution-Data-Server`) disparaissent ; lire `%TEMP%\EvolutionWin-purge.log`.
- [ ] Mise à jour vers une version supérieure avec `PURGEUSERDATA=1` sur la ligne de commande : les données restent intactes.
- [ ] `/qn AUTOUPDATE=0` : `reg query HKLM\SOFTWARE\Policies\EvolutionWin` donne `DisableAutoUpdate = 1`, plus de recherche de mise à jour ; la clé disparaît à la désinstallation.
- [ ] `/qn REGISTERMAILTO=1` : EvolutionWin apparaît dans Paramètres › Applications par défaut › Courrier / MAILTO ; après l'avoir choisi, un lien `mailto:` ouvre une rédaction. Sans la propriété, il n'apparaît pas ; l'entrée disparaît à la désinstallation.
- [ ] Désinstallation avec `/l*v u.log PURGEUSERDATA=1` : `u.log` contient « Adding PURGELOG property » ; absent sans purge.
- [ ] Installation en `/passive` ou par double-clic : la progression s'affiche (aucune interface personnalisée).
- [ ] _Non fait_ : boîte « fermer Evolution ? » en interactif et case de purge dans l'interface du MSI (validation ICE impossible sans le jeu complet de dialogues ; aucune interface en désinstallation depuis les Paramètres Windows) ; détail de la purge dans le journal MSI (il reste dans `%TEMP%\EvolutionWin-purge.log`).

## 6. Stratégies de groupe (GPO)

- [ ] Copier `deploy\policy\EvolutionWin.admx` et les dossiers `fr-FR` / `en-US` dans `C:\Windows\PolicyDefinitions` : `gpedit.msc` affiche la catégorie **EvolutionWin** avec trois stratégies.
- [ ] `DisableAutoUpdate` activée puis `gpupdate` : aucune fenêtre de mise à jour au démarrage ; Paramètres › À propos : bouton grisé avec « Les mises à jour sont désactivées par votre administrateur. »
- [ ] « Contenu distant des messages » = Jamais : Paramètres › Confidentialité, liste grisée sur « Jamais » ; désactiver la stratégie, le réglage redevient libre.
- [ ] Repli HKCU : créer `HKCU\Software\Policies\EvolutionWin\DisableAutoUpdate` (DWORD) = 1 : même effet sans clé HKLM ; avec HKLM = 0, la machine l'emporte.

## 7. Restes à valider des jalons précédents

Voir [ROADMAP.md](ROADMAP.md) : Microsoft 365 (calendrier, contacts, GAL, Teams, « De la part de »), réponses automatiques, règles serveur, file hors ligne, conflits, plages de synchronisation, indexation plein texte, anti-spam, délégations Gmail.
