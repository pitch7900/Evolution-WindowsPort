/*
 * evo-core : logique pure (sans GLib) de la file d'attente d'actions hors ligne :
 * actions différées (suppression, déplacement, copie, indicateur), fusion des actions
 * redondantes, classement des erreurs de rejeu, politique de nouvelle tentative et
 * ordre de rejeu. Fichier d'en-tête autonome, inclus par evo-core-offline-queue.c
 * et par les tests unitaires (tests/unit/test_offline_queue.c).
 */

#ifndef EVO_CORE_OFFLINE_QUEUE_UTIL_H
#define EVO_CORE_OFFLINE_QUEUE_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Types d'action différée */
#define OQ_KIND_NONE		0
#define OQ_KIND_DELETE		1
#define OQ_KIND_MOVE		2
#define OQ_KIND_COPY		3
#define OQ_KIND_FLAG		4

/* États d'une action dans la file */
#define OQ_STATUS_PENDING	0	/* en attente de rejeu */
#define OQ_STATUS_FAILED	1	/* abandonnée après trop d'échecs : conservée pour que l'utilisateur la relance ou l'annule */

/* Résultat de oq_queue_add () */
#define OQ_ADD_ADDED		0	/* action ajoutée en fin de file */
#define OQ_ADD_MERGED		1	/* action fusionnée avec une action plus ancienne (dernière valeur gagnante) */
#define OQ_ADD_DROPPED		2	/* action inutile ou impossible : ignorée */
#define OQ_ADD_FULL		3	/* file pleine */

/* Classes d'erreur de rejeu (oq_classify_error) */
#define OQ_ERR_TRANSIENT	0	/* réseau / serveur indisponible : réessayer plus tard, garder l'action */
#define OQ_ERR_GONE		1	/* le message n'existe plus : action déjà sans objet (réconciliée) */
#define OQ_ERR_PERMANENT	2	/* refus définitif (droits, dossier absent...) : compte comme un échec */

/* Indications fournies par l'appelant d'après le code GError (voir evo-core-offline-queue.c) */
#define OQ_HINT_NONE		0
#define OQ_HINT_TRANSIENT	1
#define OQ_HINT_GONE		2
#define OQ_HINT_PERMANENT	3

/* Décisions de oq_replay_decision () */
#define OQ_REPLAY_READY		0	/* à rejouer maintenant */
#define OQ_REPLAY_WAIT		1	/* nouvelle tentative différée (attente) */
#define OQ_REPLAY_SKIP		2	/* abandonnée (état FAILED) */

#define OQ_MAX_ACTIONS		2000	/* taille maximale de la file */
#define OQ_MAX_ATTEMPTS		5	/* échecs permanents tolérés avant l'état FAILED */
#define OQ_RETRY_BASE_SECONDS	30	/* délai avant la deuxième tentative */
#define OQ_RETRY_MAX_SECONDS	1800	/* délai maximal entre deux tentatives */

#define OQ_FIELD_SIZE		256
#define OQ_ERROR_SIZE		256

/**
 * @brief Action différée d'un message, enregistrée dans le journal hors ligne.
 */
typedef struct {
	int64_t id;				/* identifiant unique croissant (ordre de rejeu) */
	int kind;				/* OQ_KIND_* */
	char account[OQ_FIELD_SIZE];		/* uid du compte du message */
	char folder[OQ_FIELD_SIZE];		/* dossier du message */
	char uid[OQ_FIELD_SIZE];		/* uid du message dans le dossier */
	char dest_account[OQ_FIELD_SIZE];	/* déplacement / copie : compte de destination */
	char dest_folder[OQ_FIELD_SIZE];	/* déplacement / copie : dossier de destination */
	char flag[64];				/* indicateur : "seen", "flagged" ou étiquette */
	int value;				/* indicateur : 1 = activer, 0 = désactiver */
	char subject[OQ_FIELD_SIZE];		/* objet du message (affichage) */
	int64_t created;			/* date de mise en file (secondes Unix) */
	int attempts;				/* échecs permanents déjà subis */
	int status;				/* OQ_STATUS_* */
	int64_t next_try;			/* pas de rejeu avant cette date (secondes Unix, 0 : tout de suite) */
	char error[OQ_ERROR_SIZE];		/* dernière erreur de rejeu */
} OfflineAction;

/**
 * @brief Nom JSON d'un type d'action.
 * @param kind OQ_KIND_*
 * @return "delete", "move", "copy", "flag" ou "none"
 */
static inline const char *
oq_kind_name (int kind)
{
	switch (kind) {
	case OQ_KIND_DELETE: return "delete";
	case OQ_KIND_MOVE: return "move";
	case OQ_KIND_COPY: return "copy";
	case OQ_KIND_FLAG: return "flag";
	default: return "none";
	}
}

/**
 * @brief Type d'action d'après son nom JSON.
 * @param name nom ("delete", "move", "copy", "flag")
 * @return OQ_KIND_*, OQ_KIND_NONE si inconnu
 */
static inline int
oq_kind_from_name (const char *name)
{
	if (!name)
		return OQ_KIND_NONE;
	if (strcmp (name, "delete") == 0) return OQ_KIND_DELETE;
	if (strcmp (name, "move") == 0) return OQ_KIND_MOVE;
	if (strcmp (name, "copy") == 0) return OQ_KIND_COPY;
	if (strcmp (name, "flag") == 0) return OQ_KIND_FLAG;
	return OQ_KIND_NONE;
}

/**
 * @brief Copie bornée et toujours terminée par un zéro.
 * @param dst destination
 * @param size taille de dst
 * @param src source (NULL : chaîne vide)
 */
static inline void
oq_copy (char *dst,
	 size_t size,
	 const char *src)
{
	size_t len = src ? strlen (src) : 0;

	if (size == 0)
		return;
	if (len >= size)
		len = size - 1;
	if (len > 0)
		memcpy (dst, src, len);
	dst[len] = '\0';
}

/**
 * @brief Indique si deux actions portent sur le même message.
 * @return 1 si compte, dossier et uid sont identiques
 */
static inline int
oq_same_message (const OfflineAction *a,
		 const OfflineAction *b)
{
	return strcmp (a->account, b->account) == 0 &&
		strcmp (a->folder, b->folder) == 0 &&
		strcmp (a->uid, b->uid) == 0;
}

/**
 * @brief Indique si l'action retire le message de son dossier (suppression ou déplacement).
 */
static inline int
oq_removes_message (const OfflineAction *action)
{
	return action->kind == OQ_KIND_DELETE || action->kind == OQ_KIND_MOVE;
}

/**
 * @brief Retire l'élément d'indice index de la file en conservant l'ordre.
 * @param items tableau d'actions
 * @param count nombre d'actions (mis à jour)
 * @param index indice à retirer
 */
static inline void
oq_queue_remove_at (OfflineAction *items,
		    int *count,
		    int index)
{
	if (index < 0 || index >= *count)
		return;
	memmove (&items[index], &items[index + 1], (size_t) (*count - index - 1) * sizeof (OfflineAction));
	(*count)--;
}

/**
 * @brief Ajoute une action à la file en fusionnant les actions redondantes.
 *
 * Règles :
 *  - un message déjà promis à la suppression ou au déplacement n'accepte plus d'action
 *    (OQ_ADD_DROPPED) : il n'existe plus à son emplacement d'origine ;
 *  - un déplacement vers le dossier d'origine est ignoré ;
 *  - une suppression ou un déplacement annule les changements d'indicateurs en attente
 *    sur ce message (inutiles) ;
 *  - deux changements du même indicateur sur un message : la dernière valeur remplace
 *    la précédente (OQ_ADD_MERGED), l'ordre de rejeu de l'ancienne est conservé ;
 *  - dans tous les autres cas, l'action est ajoutée en fin de file.
 * L'identifiant id de incoming doit être renseigné par l'appelant (voir oq_next_id).
 * @param items tableau d'actions (capacité OQ_MAX_ACTIONS)
 * @param count nombre d'actions (mis à jour)
 * @param incoming nouvelle action
 * @return OQ_ADD_*
 */
static inline int
oq_queue_add (OfflineAction *items,
	      int *count,
	      const OfflineAction *incoming)
{
	int ii;

	if (incoming->kind == OQ_KIND_NONE)
		return OQ_ADD_DROPPED;

	if (incoming->kind == OQ_KIND_MOVE &&
	    strcmp (incoming->account, incoming->dest_account) == 0 &&
	    strcmp (incoming->folder, incoming->dest_folder) == 0)
		return OQ_ADD_DROPPED;

	for (ii = 0; ii < *count; ii++) {
		if (oq_same_message (&items[ii], incoming) && oq_removes_message (&items[ii]))
			return OQ_ADD_DROPPED;
	}

	if (incoming->kind == OQ_KIND_FLAG) {
		for (ii = 0; ii < *count; ii++) {
			if (items[ii].kind == OQ_KIND_FLAG && oq_same_message (&items[ii], incoming) &&
			    strcmp (items[ii].flag, incoming->flag) == 0) {
				items[ii].value = incoming->value;
				items[ii].attempts = 0;
				items[ii].status = OQ_STATUS_PENDING;
				items[ii].next_try = 0;
				items[ii].error[0] = '\0';
				return OQ_ADD_MERGED;
			}
		}
	}

	if (oq_removes_message (incoming)) {
		for (ii = *count - 1; ii >= 0; ii--) {
			if (items[ii].kind == OQ_KIND_FLAG && oq_same_message (&items[ii], incoming))
				oq_queue_remove_at (items, count, ii);
		}
	}

	if (*count >= OQ_MAX_ACTIONS)
		return OQ_ADD_FULL;

	items[*count] = *incoming;
	(*count)++;
	return OQ_ADD_ADDED;
}

/**
 * @brief Prochain identifiant d'action (supérieur à tous ceux de la file).
 * @param items tableau d'actions
 * @param count nombre d'actions
 * @param last_id dernier identifiant attribué (persistant, 0 au départ)
 * @return identifiant à attribuer
 */
static inline int64_t
oq_next_id (const OfflineAction *items,
	    int count,
	    int64_t last_id)
{
	int ii;
	int64_t max_id = last_id;

	for (ii = 0; ii < count; ii++) {
		if (items[ii].id > max_id)
			max_id = items[ii].id;
	}

	return max_id + 1;
}

/**
 * @brief Recherche une action par identifiant.
 * @return indice dans la file, -1 si absente
 */
static inline int
oq_queue_find (const OfflineAction *items,
	       int count,
	       int64_t id)
{
	int ii;

	for (ii = 0; ii < count; ii++) {
		if (items[ii].id == id)
			return ii;
	}

	return -1;
}

/**
 * @brief Indique si un message est promis à la suppression ou au déplacement (à masquer de la liste).
 * @param items tableau d'actions
 * @param count nombre d'actions
 * @param account compte du message
 * @param folder dossier du message
 * @param uid uid du message
 * @return 1 si une action en attente retire ce message de son dossier
 */
static inline int
oq_is_message_pending_removal (const OfflineAction *items,
			       int count,
			       const char *account,
			       const char *folder,
			       const char *uid)
{
	int ii;

	for (ii = 0; ii < count; ii++) {
		if (oq_removes_message (&items[ii]) && strcmp (items[ii].account, account) == 0 &&
		    strcmp (items[ii].folder, folder) == 0 && strcmp (items[ii].uid, uid) == 0)
			return 1;
	}

	return 0;
}

/**
 * @brief Sous-chaîne insensible à la casse (ASCII).
 * @param text texte examiné
 * @param needle mot recherché
 * @return 1 si needle figure dans text
 */
static inline int
oq_contains_nocase (const char *text,
		    const char *needle)
{
	size_t nlen = strlen (needle), ii, jj;

	for (ii = 0; text[ii]; ii++) {
		for (jj = 0; jj < nlen; jj++) {
			char a = text[ii + jj], b = needle[jj];

			if (!a)
				return 0;
			if (a >= 'A' && a <= 'Z') a = (char) (a + 32);
			if (b >= 'A' && b <= 'Z') b = (char) (b + 32);
			if (a != b)
				break;
		}
		if (jj == nlen)
			return 1;
	}

	return 0;
}

/**
 * @brief Classe l'erreur d'un rejeu : à réessayer, déjà sans objet ou définitive.
 *
 * L'indication tirée du code d'erreur (OQ_HINT_*) prime ; sans indication, le texte
 * du message est examiné (anglais de Camel/GIO et français).
 * @param hint indication OQ_HINT_* de l'appelant
 * @param message texte de l'erreur (peut être NULL)
 * @return OQ_ERR_*
 */
static inline int
oq_classify_error (int hint,
		   const char *message)
{
	static const char *const transient_words[] = {
		"offline", "not connected", "timed out", "timeout", "unreachable", "connection refused",
		"connection reset", "connection closed", "network", "temporarily", "try again",
		"service unavailable", "server busy", "hors connexion", "hors ligne", "delai", "d\xC3\xA9lai",
		"could not connect", "cannot connect", "throttl", NULL
	};
	static const char *const gone_words[] = {
		"not found", "no such message", "does not exist", "doesn't exist", "no longer exists",
		"unknown message", "introuvable", "n'existe plus", "invalid uid", NULL
	};
	int ii;

	if (hint == OQ_HINT_TRANSIENT)
		return OQ_ERR_TRANSIENT;
	if (hint == OQ_HINT_GONE)
		return OQ_ERR_GONE;
	if (hint == OQ_HINT_PERMANENT)
		return OQ_ERR_PERMANENT;

	if (message && *message) {
		for (ii = 0; transient_words[ii]; ii++) {
			if (oq_contains_nocase (message, transient_words[ii]))
				return OQ_ERR_TRANSIENT;
		}
		for (ii = 0; gone_words[ii]; ii++) {
			if (oq_contains_nocase (message, gone_words[ii]))
				return OQ_ERR_GONE;
		}
	}

	return OQ_ERR_PERMANENT;
}

/**
 * @brief Délai avant la prochaine tentative après « attempts » échecs (30 s, 60 s, 120 s... plafonné).
 * @param attempts nombre d'échecs déjà subis (>= 1)
 * @return délai en secondes
 */
static inline int64_t
oq_retry_delay (int attempts)
{
	int64_t delay = OQ_RETRY_BASE_SECONDS;
	int ii;

	for (ii = 1; ii < attempts && delay < OQ_RETRY_MAX_SECONDS; ii++)
		delay *= 2;

	return delay > OQ_RETRY_MAX_SECONDS ? OQ_RETRY_MAX_SECONDS : delay;
}

/**
 * @brief Enregistre l'échec d'un rejeu : planifie une nouvelle tentative ou abandonne l'action.
 *
 * Une erreur transitoire (réseau) n'use pas les tentatives : l'action reste prête et
 * sera rejouée au prochain retour du réseau. Une erreur définitive incrémente le
 * compteur ; au-delà de OQ_MAX_ATTEMPTS l'action passe à OQ_STATUS_FAILED.
 * @param action action concernée (modifiée)
 * @param error_class OQ_ERR_PERMANENT ou OQ_ERR_TRANSIENT
 * @param message texte de l'erreur
 * @param now date courante (secondes Unix)
 */
static inline void
oq_record_failure (OfflineAction *action,
		   int error_class,
		   const char *message,
		   int64_t now)
{
	oq_copy (action->error, sizeof (action->error), message);

	if (error_class == OQ_ERR_TRANSIENT)
		return;

	action->attempts++;
	if (action->attempts >= OQ_MAX_ATTEMPTS) {
		action->status = OQ_STATUS_FAILED;
		action->next_try = 0;
	} else {
		action->next_try = now + oq_retry_delay (action->attempts);
	}
}

/**
 * @brief Décide si une action doit être rejouée maintenant.
 * @param action action examinée
 * @param now date courante (secondes Unix)
 * @param force 1 si l'utilisateur demande un rejeu immédiat (ignore l'attente)
 * @return OQ_REPLAY_*
 */
static inline int
oq_replay_decision (const OfflineAction *action,
		    int64_t now,
		    int force)
{
	if (action->status == OQ_STATUS_FAILED && !force)
		return OQ_REPLAY_SKIP;
	if (!force && action->next_try > now)
		return OQ_REPLAY_WAIT;
	return OQ_REPLAY_READY;
}

/**
 * @brief Relance une action abandonnée (remet les compteurs à zéro).
 * @param action action concernée (modifiée)
 */
static inline void
oq_action_reset (OfflineAction *action)
{
	action->status = OQ_STATUS_PENDING;
	action->attempts = 0;
	action->next_try = 0;
	action->error[0] = '\0';
}

/**
 * @brief Nombre d'actions en attente de rejeu (hors abandonnées).
 * @param items tableau d'actions
 * @param count nombre d'actions
 * @return nombre d'actions à l'état OQ_STATUS_PENDING
 */
static inline int
oq_count_pending (const OfflineAction *items,
		  int count)
{
	int ii, pending = 0;

	for (ii = 0; ii < count; ii++) {
		if (items[ii].status == OQ_STATUS_PENDING)
			pending++;
	}

	return pending;
}

#endif /* EVO_CORE_OFFLINE_QUEUE_UTIL_H */
