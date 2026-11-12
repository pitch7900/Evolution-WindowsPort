# Déploiement d'entreprise d'EvolutionWin

## Installation silencieuse du MSI

```
msiexec /i Evolution-<version>-x64.msi /qn [FORCECLOSE=1] [AUTOUPDATE=0] [REGISTERMAILTO=1] [LAUNCHAPP=1] [UILANGUAGE=fr-FR] /l*v install.log
```

| Propriété | Défaut | Effet |
|---|---|---|
| `FORCECLOSE` | `1` | Ferme EvolutionWin (demande propre, 5 s, puis arrêt), les services EDS (`evolution-source-registry`, fabriques carnet d'adresses / agenda et sous-processus, `gdbus`), puis `camel-index-control`, `gspawn-win64-helper` et les processus `msedgewebview2.exe` du dossier de données d'EvolutionWin, **dans toutes les sessions** (serveur multi-sessions). `FORCECLOSE=0` : aucune fermeture (fichiers verrouillés remplacés au redémarrage, code de sortie 3010). |
| `PURGEUSERDATA` | `0` | À la **désinstallation** seulement (`msiexec /x … PURGEUSERDATA=1`) : supprime les données de l'utilisateur qui désinstalle (`%LOCALAPPDATA%\EvolutionWin`, `%LOCALAPPDATA%\evolution`, `%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution`, identifiants `Evolution-Data-Server:*`). Jamais lors d'une mise à jour. Journal : `%TEMP%\EvolutionWin-purge.log`. Les autres profils utilisateur ne sont pas touchés : supprimer leurs dossiers à la main. |
| `AUTOUPDATE` | _(non définie)_ | `0` : écrit la stratégie `HKLM\SOFTWARE\Policies\EvolutionWin\DisableAutoUpdate = 1` (voir plus bas) ; supprimée à la désinstallation. |
| `REGISTERMAILTO` | `0` | `1` : déclare EvolutionWin comme application de messagerie de la machine (`HKLM\SOFTWARE\Clients\Mail\EvolutionWin`, `RegisteredApplications`, classe `EvolutionWin.Url.mailto`). Windows 11 interdit de fixer l'application par défaut par programme : l'utilisateur choisit EvolutionWin dans Paramètres › Applications par défaut. |
| `LAUNCHAPP` | `0` | `1` : relance EvolutionWin pour l'utilisateur courant à la fin de l'installation. |
| `UILANGUAGE` | `fr-FR` | Langue par défaut de l'interface. |

Le journal MSI (`/l*v`) contient, lignes « EvolutionWin purge : … », chaque élément supprimé quand la purge est demandée (sortie de `WixQuietExec`) ; le même détail est dans `%TEMP%\EvolutionWin-purge.log`.

**Interface interactive** (double-clic sur le MSI, `msiexec /i … ` sans `/q`) : dialogues standard WixUI_InstallDir (sans licence), plus la page « Fermeture d'Evolution » (fermer automatiquement, par défaut, ou laisser Evolution ouvert : `FORCECLOSE=0`) et, en rouvrant le MSI déjà installé puis « Supprimer », la page « Données d'Evolution » avec la case « Supprimer aussi mes comptes et données » (`PURGEUSERDATA=1`). **Limite** : _Paramètres › Applications installées_ lance la désinstallation sans interface : la case n'y est pas proposée (utiliser `msiexec /x … PURGEUSERDATA=1`). Mode silencieux (`/qn`) : aucune page, propriétés sur la ligne de commande. Interface en français uniquement.

Redémarrage et Restart Manager sont désactivés (`REBOOT=ReallySuppress`, `MSIRESTARTMANAGERCONTROL=Disable`).

## Stratégies de groupe (GPO)

Le modèle d'administration est dans `deploy\policy` (et dans `C:\Program Files\Evolution\policy` après installation) :
copier `EvolutionWin.admx` et les dossiers `fr-FR` / `en-US` dans `C:\Windows\PolicyDefinitions`
(ou dans le magasin central du domaine). Catégorie : **EvolutionWin**.

Les stratégies sont lues dans `HKLM\SOFTWARE\Policies\EvolutionWin`, puis (repli) `HKCU\Software\Policies\EvolutionWin`.
La machine l'emporte sur l'utilisateur.

| Valeur | Type | Effet |
|---|---|---|
| `DisableAutoUpdate` | DWORD 0/1 | `1` : aucune vérification ni installation de mise à jour (le bouton de Paramètres › À propos est grisé). |
| `Settings\<clé>` | DWORD ou chaîne | Impose un réglage : la valeur l'emporte sur `settings.json` et le réglage est verrouillé (grisé quand l'interface le gère). `<clé>` est une clé de `settings.json` (ex. `privacy.remote-content` = `never` / `trusted` / `always`, `tasks.show-completed` = 0/1). |

Sans modèle ADMX (Intune, script) : écrire directement ces valeurs de registre.
