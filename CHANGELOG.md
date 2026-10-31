# Journal des modifications

Toutes les évolutions notables d'EvolutionWin (portage natif Windows 11
d'Evolution) sont décrites ici, par version. La version en cours est dans
le fichier [`VERSION`](VERSION) ; c'est aussi celle de l'installeur
(`tools\build-msi.bat`). Le détail technique de chaque étape est dans
[`WINDOWS-PORT.md`](WINDOWS-PORT.md), les tâches restantes dans
[`TODO.md`](TODO.md).

Format : [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/),
numérotation [SemVer](https://semver.org/lang/fr/).

## [0.9.2] — en cours

### Ajouté

- Icône de l'application (celle d'Evolution) : exécutable, barre de titre,
  barre des tâches, Alt+Tab, raccourci du menu Démarrer et « Applications
  installées ».
- Fichiers `VERSION` et `CHANGELOG.md` ; `build-msi.bat` prend sa version
  dans `VERSION` par défaut.

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

[0.9.2]: https://github.com/pitch7900/Evolution-WindowsPort/compare/a81cf2e...HEAD
[0.9.1]: https://github.com/pitch7900/Evolution-WindowsPort/commit/a81cf2e
[0.9.0]: https://github.com/pitch7900/Evolution-WindowsPort/commit/8fcd525
[0.8.0]: https://github.com/pitch7900/Evolution-WindowsPort/commit/7d0b54d
