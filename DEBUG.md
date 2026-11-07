<!-- cspell:disable -->
# Fichier de Debug

## Messages d'erreurs et logs

````text
%LOCALAPPDATA%\EvolutionWin\
````

## Répertoire de configuration des comptes

````text
%LOCALAPPDATA%\evolution\sources\
````

## Localisation des WER (dump)

````text
%PROGRAMDATA%\Microsoft\Windows\WER\
````

## Caches et données hors ligne

Le répertoire de cache est celui de GLib sous Windows (`g_get_user_cache_dir`), c'est-à-dire le cache Internet de Windows, **pas** `%LOCALAPPDATA%\evolution`.

### Messages des comptes (copies locales, mode hors connexion)

````text
%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\evolution-windows\mail\<uid du compte>\
````

- `<uid du compte>` est le nom du fichier `.source` du compte dans `%LOCALAPPDATA%\evolution\sources\` (le nom affiché est dans la ligne `DisplayName=`).
- Dans chaque compte : `folders.db` (index des messages), `folder-tree` (arborescence) et `folders\<dossier>\cur\` (corps des messages téléchargés pour la lecture hors connexion). Un dossier absent de `cur\` n'a rien de téléchargé.
- Supprimer le dossier d'un compte (application fermée) force une resynchronisation complète au prochain lancement.

### Calendriers, carnets d'adresses, tâches et mémos

````text
%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\calendar\
%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\addressbook\
%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\tasks\
%LOCALAPPDATA%\Microsoft\Windows\INetCache\evolution\memos\
````

Un sous-dossier par source (uid), comme pour les comptes de messagerie.

### Dossiers locaux et données propres à l'application

````text
%LOCALAPPDATA%\evolution\evolution-windows\mail\         (dossiers locaux, certificats approuvés)
%LOCALAPPDATA%\evolution\mail\local\                     (dossiers locaux d'Evolution)
%LOCALAPPDATA%\EvolutionWin\settings.json                (réglages de l'application)
````

Le dernier résultat du bouton « Synchroniser maintenant » est mémorisé dans `settings.json` (clés `offline-sync.<uid du compte>.<dossier>`).
