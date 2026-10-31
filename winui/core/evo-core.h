/*
 * evo-core: mail engine of the native Windows client (EvolutionWin).
 *
 * Wraps evolution-data-server (accounts, ESourceRegistry) and Camel (IMAP,
 * POP3, SMTP, local folders) behind a small C API without any GLib type, so
 * that the C++/WinRT user interface does not depend on GLib.
 *
 * Threading: the core owns a thread running a GLib main loop; every EDS and
 * Camel object lives there, network operations run in worker threads. All
 * callbacks are invoked on one of these threads, never on the caller's: the
 * user interface must hand them over to its own thread.
 *
 * Results are UTF-8 JSON documents, described with each function.
 */

#ifndef EVO_CORE_H
#define EVO_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _EvoCore EvoCore;

/* Result of a request: exactly one of json and error_message is non-NULL.
 * Both are only valid during the call. */
typedef void (*EvoCoreResultFunc) (const char *json,
				   const char *error_message,
				   void *user_data);

/* Events of the core:
 *   "ready"   {}                         the accounts are loaded
 *   "error"   {"message": "..."}         the core could not start
 *   "status"  {"message": "..."}         progress information
 *   "folder-changed" {"account": "...", "folder": "...", "unread": 3,
 *                     "total": 120, "added": 1, "removed": 0, "changed": 2}
 *                                        messages of an opened folder changed
 *   "folders-updated" {"account": "..."} folder counts may have changed:
 *                                        list the folders again
 *   "new-mail" {"account": "...", "folder": "...", "count": 2,
 *               "from": "...", "subject": "..."}
 *                                        new unread messages arrived in Inbox
 *   "sending" / "sent" {"subject": "..."} a message is being / was sent
 */
typedef void (*EvoCoreEventFunc) (const char *event,
				  const char *json,
				  void *user_data);

/* Starts the core thread; "ready" or "error" follows. */
EvoCore *	evo_core_new			(EvoCoreEventFunc event_func,
						 void *user_data);
/* Stops the core thread and frees the core; no callback is invoked
 * after it returns. */
void		evo_core_free			(EvoCore *core);

/* [{"uid": "...", "name": "Display name", "address": "user@example.com",
 *   "protocol": "imapx", "local": false}, ...] */
void		evo_core_list_accounts		(EvoCore *core,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Connects the account if needed, then its folder tree:
 * [{"full_name": "INBOX", "name": "Inbox", "unread": 3, "total": 120,
 *   "type": "inbox"|"outbox"|"sent"|"drafts"|"trash"|"junk"|"archive"|"",
 *   "children": [...]}, ...] */
void		evo_core_list_folders		(EvoCore *core,
						 const char *account_uid,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Refreshes the folder, then its messages, most recent first (at most
 * max_count, 0 for all):
 * [{"uid": "...", "subject": "...", "from": "Name <a@b>", "date": 1730000000,
 *   "seen": true, "flagged": false, "attachments": false}, ...] */
void		evo_core_list_messages		(EvoCore *core,
						 const char *account_uid,
						 const char *folder_full_name,
						 unsigned int max_count,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Downloads the message, marks it as read, and returns it ready to display:
 * {"subject": "...", "from": "...", "to": "...", "cc": "...", "date": 1730000000,
 *  "reply_to": "...", "message_id": "<...>", "references": "<...> <...>",
 *  "html": "<html>...</html>",  (the body; plain text is converted to HTML)
 *  "text": "..."                 (the body as plain text, to quote in replies)
 * } */
void		evo_core_get_message		(EvoCore *core,
						 const char *account_uid,
						 const char *folder_full_name,
						 const char *message_uid,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Checks for new mail now in every account (also done every 5 minutes);
 * "folder-changed" / "folders-updated" events follow. */
void		evo_core_refresh		(EvoCore *core);

/* Envoie un message avec l'identité et le serveur SMTP d'un compte, puis
 * l'enregistre dans le dossier Envoyés du compte. Format attendu pour message_json :
 * {"account": "<account uid>", "to": "a@b, Name <c@d>", "cc": "", "bcc": "",
 *  "subject": "...", "text": "corps en texte brut",
 *  "html": "<html>...</html>" (optionnel, génère un multipart/alternative),
 *  "attachments": ["C:\\chemin\\fichier", ...] (optionnel),
 *  "in_reply_to": "<message id>", "references": "<...>" (optionnel)}
 * Le résultat est {"saved": true|false} (copie enregistrée dans Envoyés). */
void		evo_core_send_message		(EvoCore *core,
						 const char *message_json,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Modifie un indicateur d'un message ("seen" ou "flagged") et synchronise le
 * dossier avec le serveur.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier
 *   message_uid      : identifiant du message dans le dossier
 *   flag_name        : nom de l'indicateur ("seen" ou "flagged")
 *   value            : 1 pour activer l'indicateur, 0 pour le retirer
 *   func             : rappel recevant {"seen": true|false, "flagged": true|false}
 *   user_data        : données utilisateur transmises au rappel */
void		evo_core_set_message_flags	(EvoCore *core,
						 const char *account_uid,
						 const char *folder_full_name,
						 const char *message_uid,
						 const char *flag_name,
						 int value,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Supprime un message : le déplace vers la corbeille du compte si elle existe
 * et que le message ne s'y trouve pas déjà, sinon le marque comme supprimé et
 * purge le dossier.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier contenant le message
 *   message_uid      : identifiant du message à supprimer
 *   func             : rappel recevant {"trashed": true|false}
 *   user_data        : données utilisateur transmises au rappel */
void		evo_core_delete_message		(EvoCore *core,
						 const char *account_uid,
						 const char *folder_full_name,
						 const char *message_uid,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Déplace un message d'un dossier vers un autre dossier.
 * Paramètres :
 *   core                  : instance du moteur evo-core
 *   src_account_uid       : identifiant du compte source
 *   src_folder_full_name  : chemin complet du dossier source
 *   message_uid           : identifiant du message à déplacer
 *   dest_account_uid      : identifiant du compte de destination
 *   dest_folder_full_name : chemin complet du dossier de destination
 *   func                  : rappel recevant {}
 *   user_data             : données utilisateur transmises au rappel */
void		evo_core_move_message		(EvoCore *core,
						 const char *src_account_uid,
						 const char *src_folder_full_name,
						 const char *message_uid,
						 const char *dest_account_uid,
						 const char *dest_folder_full_name,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Recherche dans un dossier les messages correspondant à la requête (objet,
 * expéditeur, destinataires ou corps du message), triés du plus récent au plus
 * ancien.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier dans lequel chercher
 *   query            : texte recherché (mots séparés par des espaces)
 *   max_count        : nombre maximal de messages renvoyés (0 pour tous)
 *   func             : rappel recevant le tableau JSON des messages
 *   user_data        : données utilisateur transmises au rappel */
void		evo_core_search_messages	(EvoCore *core,
						 const char *account_uid,
						 const char *folder_full_name,
						 const char *query,
						 unsigned int max_count,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Enregistre une pièce jointe d'un message dans un fichier sur le disque.
 * Paramètres :
 *   core             : instance du moteur evo-core
 *   account_uid      : identifiant du compte
 *   folder_full_name : chemin complet du dossier contenant le message
 *   message_uid      : identifiant du message
 *   attachment_index : indice de la pièce jointe (tel que renvoyé par evo_core_get_message)
 *   dest_path        : chemin complet du fichier de destination en UTF-8
 *   func             : rappel recevant {"size": <octets écrits>}
 *   user_data        : données utilisateur transmises au rappel */
void		evo_core_save_attachment	(EvoCore *core,
						 const char *account_uid,
						 const char *folder_full_name,
						 const char *message_uid,
						 unsigned int attachment_index,
						 const char *dest_path,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Exporte un ou plusieurs messages d'un dossier sous forme de fichiers .eml
 * (RFC 822) dans un répertoire temporaire afin de les joindre à un courriel.
 * Format attendu pour request_json :
 * {"account": "...", "folder": "...", "uids": ["uid1", "uid2", ...]}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   request_json : requête JSON UTF-8 contenant le compte, le dossier et les UID
 *   func         : rappel recevant ["C:\\...\\Message1.eml", ...]
 *   user_data    : données utilisateur transmises au rappel */
void		evo_core_export_messages_eml	(EvoCore *core,
						 const char *request_json,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Enregistre le mot de passe d'un compte ou serveur dans le gestionnaire
 * d'identifiants Windows via evolution-data-server.
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   source_uid : identifiant de la source (compte ou serveur d'envoi)
 *   password   : mot de passe en UTF-8
 *   func       : rappel recevant {}
 *   user_data  : données utilisateur transmises au rappel */
void		evo_core_save_password		(EvoCore *core,
						 const char *source_uid,
						 const char *password,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Crée et enregistre un nouveau compte de messagerie IMAP / SMTP dans
 * evolution-data-server, puis l'ajoute à la session courante.
 * Format attendu pour account_json :
 * {"name": "...", "real_name": "...", "address": "user@example.com",
 *  "imap_host": "...", "imap_port": 993, "imap_user": "...",
 *  "imap_security": "tls"|"starttls"|"none",
 *  "smtp_host": "...", "smtp_port": 465, "smtp_user": "...",
 *  "smtp_security": "tls"|"starttls"|"none",
 *  "password": "..."}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_json : configuration du compte au format JSON UTF-8
 *   func         : rappel recevant {"uid": "<uid du compte créé>"}
 *   user_data    : données utilisateur transmises au rappel */
void		evo_core_add_account		(EvoCore *core,
						 const char *account_json,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Crée un compte Microsoft 365 / Exchange Online (Microsoft Graph, via
 * evolution-ews) : courrier, agendas et carnets d'adresses. La connexion se
 * fait ensuite par evo_core_oauth2_begin () sur "collection_uid".
 * Format attendu pour account_json :
 * {"email": "...", "display_name": "...", "full_name": "...",
 *  "client_id": "..." (facultatif : application Entra ID propre),
 *  "tenant": "..." (facultatif : "common" par défaut)}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   account_json : configuration du compte au format JSON UTF-8
 *   func         : rappel recevant {"uid": "...", "collection_uid": "..."}
 *   user_data    : données utilisateur transmises au rappel */
void		evo_core_add_microsoft365_account
						(EvoCore *core,
						 const char *account_json,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Connexion OAuth2 interactive d'un compte (Microsoft 365...), en deux
 * étapes, aussi en réponse à l'événement "oauth2-required"
 * ({"source_uid", "name", "user"}) :
 * 1. evo_core_oauth2_begin : rappel recevant {"source_uid", "uri",
 *    "redirect_uri", "service"} ; ouvrir "uri" dans un navigateur intégré ;
 * 2. dès qu'une navigation atteint "redirect_uri" (même hôte et chemin),
 *    la transmettre complète à evo_core_oauth2_finish : le code est échangé
 *    et les jetons enregistrés (rappel recevant {}), puis la relève reprend.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   source_uid   : uid du compte ou de sa collection / source_uid renvoyé
 *   redirect_uri : adresse de redirection atteinte, avec ses paramètres
 *   func         : rappel du résultat
 *   user_data    : données utilisateur transmises au rappel */
void		evo_core_oauth2_begin		(EvoCore *core,
						 const char *source_uid,
						 EvoCoreResultFunc func,
						 void *user_data);
void		evo_core_oauth2_finish		(EvoCore *core,
						 const char *source_uid,
						 const char *redirect_uri,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Supprime un compte de messagerie (compte de réception, identité et serveur
 * d'envoi associé) d'evolution-data-server et de la session courante.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   account_uid : identifiant du compte à supprimer
 *   func        : rappel recevant {}
 *   user_data   : données utilisateur transmises au rappel */
void		evo_core_remove_account		(EvoCore *core,
						 const char *account_uid,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Liste les événements des agendas activés dans l'intervalle [start_unix, end_unix]
 * (ou tous les événements si start_unix <= 0 ou end_unix <= 0).
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   start_unix : début de la période en secondes Unix (UTC)
 *   end_unix   : fin de la période en secondes Unix (UTC)
 *   func       : rappel recevant [{"uid": "...", "summary": "...", "location": "...",
 *                "description": "...", "start": 1730000000, "end": 1730003600,
 *                "calendar_uid": "...", "calendar_name": "..."}, ...]
 *   user_data  : données utilisateur transmises au rappel */
void		evo_core_list_events		(EvoCore *core,
						 long long start_unix,
						 long long end_unix,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Crée un nouvel événement dans l'agenda par défaut (ou celui indiqué par
 * "calendar_uid"). Format attendu pour event_json :
 * {"calendar_uid": "..." (optionnel), "summary": "...", "location": "...",
 *  "description": "...", "start": 1730000000, "end": 1730003600}
 * Paramètres :
 *   core       : instance du moteur evo-core
 *   event_json : description de l'événement au format JSON UTF-8
 *   func       : rappel recevant {"uid": "<uid de l'événement>"}
 *   user_data  : données utilisateur transmises au rappel */
void		evo_core_create_event		(EvoCore *core,
						 const char *event_json,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Supprime un événement d'un agenda.
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   calendar_uid : identifiant de la source d'agenda (ou "" pour l'agenda par défaut)
 *   event_uid    : identifiant de l'événement à supprimer
 *   func         : rappel recevant {}
 *   user_data    : données utilisateur transmises au rappel */
void		evo_core_delete_event		(EvoCore *core,
						 const char *calendar_uid,
						 const char *event_uid,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Liste ou recherche les contacts des carnets d'adresses activés.
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   query     : filtre de recherche (chaîne vide ou NULL pour tous les contacts)
 *   func      : rappel recevant [{"uid": "...", "full_name": "...", "email": "...",
 *               "phone": "...", "org": "...", "notes": "...",
 *               "book_uid": "...", "book_name": "..."}, ...]
 *   user_data : données utilisateur transmises au rappel */
void		evo_core_list_contacts		(EvoCore *core,
						 const char *query,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Crée un nouveau contact dans le carnet d'adresses par défaut (ou celui
 * indiqué par "book_uid"). Format attendu pour contact_json :
 * {"book_uid": "..." (optionnel), "full_name": "...", "email": "...",
 *  "phone": "...", "org": "...", "notes": "..."}
 * Paramètres :
 *   core         : instance du moteur evo-core
 *   contact_json : description du contact au format JSON UTF-8
 *   func         : rappel recevant {"uid": "<uid du contact>"}
 *   user_data    : données utilisateur transmises au rappel */
void		evo_core_create_contact		(EvoCore *core,
						 const char *contact_json,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Supprime un contact d'un carnet d'adresses.
 * Paramètres :
 *   core        : instance du moteur evo-core
 *   book_uid    : identifiant du carnet d'adresses (ou "" pour le carnet par défaut)
 *   contact_uid : identifiant du contact à supprimer
 *   func        : rappel recevant {}
 *   user_data   : données utilisateur transmises au rappel */
void		evo_core_delete_contact		(EvoCore *core,
						 const char *book_uid,
						 const char *contact_uid,
						 EvoCoreResultFunc func,
						 void *user_data);

/* Répond à une demande interactive d'approbation de certificat TLS émise via
 * l'événement "tls-trust-required".
 * Paramètres :
 *   core           : instance du moteur evo-core
 *   trust_id       : identifiant unique de la demande ("host|fingerprint")
 *   trust_decision : 0 = refuser, 1 = accepter pour la session, 2 = toujours faire confiance */
void		evo_core_respond_tls_trust	(EvoCore *core,
						 const char *trust_id,
						 int trust_decision);

/* Versions des composants du moteur, pour la boîte « À propos » et les
 * rapports de bug (sans instance du moteur, appel synchrone) :
 * {"evolution_data_server": "3.63.1", "evolution_ews": "3.63.1" (vide si
 *  non installé), "glib": "2.86.0", "libsoup": "3.6.5"}
 * Retourne : chaîne JSON statique, à ne pas libérer. */
const char *	evo_core_version_info		(void);

#ifdef __cplusplus
}
#endif

#endif /* EVO_CORE_H */
