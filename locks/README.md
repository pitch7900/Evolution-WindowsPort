# Registre des Verrous de Tâches pour Agents IA (Anti-Collision Locale)

Ce dossier sert de registre de coordination pour les agents IA travaillant en parallèle sur la machine locale dans cet espace de travail.

## Fonctionnement
1. Les fichiers `locks/*.json` sont exclus de Git via `.gitignore` pour éviter de polluer l'historique des commits.
2. Lorsqu'un agent prend en charge une tâche :
   - Il inspecte les fichiers JSON présents dans ce dossier.
   - Si les fichiers ciblés ne sont pas verrouillés, il crée son fichier : `locks/<agent_id>_<task_id>.json`.
   - Il y déclare la tâche en cours, les fichiers modifiés et les fonctions ciblées.
3. Dès que sa tâche est achevée, validée par les tests et prête à être commitée, l'agent **supprime son fichier de verrou** de ce dossier.
