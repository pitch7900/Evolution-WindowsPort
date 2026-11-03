# Registre des Verrous de Tâches pour Agents IA (Anti-Collision)

Ce dossier sert de registre décentralisé pour la coordination asynchrone des agents IA travaillant en parallèle sur le projet.

## Fonctionnement
Lorsqu'un agent prend en charge une tâche :
1. Il effectue un `git pull --rebase` pour s'assurer d'avoir les derniers locks à jour.
2. Il crée un fichier JSON unique : `locks/<agent_id>_<timestamp>.json`.
3. Il y déclare la tâche traitée, les fichiers ciblés et les portions concernées.
4. Dès que la tâche est terminée, testée et commitée, il **supprime** son fichier de verrou dans le même commit.

Si un lock a plus de 6 heures sans activité, il peut être considéré comme abandonné ou expiré par un autre agent.
