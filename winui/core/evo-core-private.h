/*
 * evo-core : en-tete prive partage par les fichiers sources du moteur
 * (evo-core*.c). Ne fait pas partie de l'interface publique : voir evo-core.h.
 *
 * Il regroupe les includes, la structure du moteur, le type de session Camel,
 * les types communs a plusieurs modules et les prototypes des fonctions
 * internes (prefixe core_) utilisees d'un fichier a l'autre.
 */

#ifndef EVO_CORE_PRIVATE_H
#define EVO_CORE_PRIVATE_H

/*
 * evo-core: mail engine of the native Windows client, see evo-core.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <windns.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <camel/camel.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <libedataserver/libedataserver.h>
#include <libecal/libecal.h>
#include <libebook/libebook.h>
#include "evo-core.h"
struct _EvoCore {
	EvoCoreEventFunc event_func;
	gpointer event_data;

	GThread *thread;
	GMainContext *context;
	GMainLoop *loop;
	GThreadPool *pool;
	GCancellable *cancellable;

	ESourceRegistry *registry;
	CamelSession *session;

	/* opened folders, "<account>\x1F<full name>" ~> CamelFolder *;
	 * their "changed" signal becomes the "folder-changed" event */
	GHashTable *folders;
	GMutex folders_lock;
	GSource *refresh_source;

	/* serializes core_ensure_connected () */
	GMutex connect_lock;

	/* startup state, protected by lock */
	GMutex lock;
	GCond cond;
	gboolean started;
	gchar *start_error;

	/* OAuth2 sign-ins in progress (evo_core_oauth2_begin/finish),
	 * credentials source uid ~> CoreOAuth2Pending *, protected by lock */
	GHashTable *oauth2_pending;
	GHashTable *book_clients;	/* uid -> EBookClient (autocomplétion) */
};

/* CamelSession configuring its services from their ESource, and
 * authenticating them with the password saved by evolution-data-server
 * (Windows Credential Manager). */

#define EVO_TYPE_CORE_SESSION (evo_core_session_get_type ())

G_DECLARE_FINAL_TYPE (EvoCoreSession, evo_core_session, EVO, CORE_SESSION, CamelSession)

struct _EvoCoreSession {
	CamelSession parent;

	EvoCore *core;
	ESourceRegistry *registry;
	ESourceCredentialsProvider *credentials_provider;
	GHashTable *prompted_sources;
	GMutex prompted_lock;

	/* Gestion des certificats TLS approuvés ou en attente de décision */
	GHashTable *trusted_certs;
	GHashTable *pending_trust;
	GMutex trust_lock;
	GCond trust_cond;
	gchar *trust_file;
};

typedef gchar * (* RequestFunc) (EvoCore *core,
				 gchar **args,
				 GError **error);

/* --- agenda (ECalClient / libecal-2.0) --- */

typedef struct {
	gchar *uid;
	gchar *summary;
	gchar *location;
	gchar *description;
	gint64 start;
	gint64 end;
	gchar *calendar_uid;
	gchar *calendar_name;
	gchar *calendar_color;	/* "#rrggbb" (ESourceSelectable) ou NULL */
	gboolean all_day;
	gboolean recurring;
	gint reminder_minutes;	/* rappel avant le début (minutes), -1 : aucun */
	gboolean is_private;	/* CLASS:PRIVATE ou CONFIDENTIAL */
	const gchar *show_as;	/* free, working, tentative, busy, oof (statique) */
	gint priority;	/* PRIORITY iCalendar (0 : aucune) */
	gchar **categories;	/* noms de catégories (NULL-terminé) */
} CalEventItem;

/* Catégories iCalendar communes aux événements et aux tâches (evo-core-calendar.c) */
gchar **
ical_dup_categories (ICalComponent *icomp);
void
builder_add_categories (JsonBuilder *builder,
			const gchar * const *names);
void
ical_add_categories (JsonBuilder *builder,
		     ICalComponent *icomp);
void
ical_apply_categories_json (ICalComponent *icomp,
			    JsonObject *object);

/* --- exportation vCard / iCalendar --- */

/* Types de sources (agendas, tâches, mémos, carnets d'adresses) et
 * extensions ESource correspondantes. */
static const struct {
	const gchar *kind;
	const gchar *extension;
} source_kinds[] = {
	{ "calendar", E_SOURCE_EXTENSION_CALENDAR },
	{ "tasks", E_SOURCE_EXTENSION_TASK_LIST },
	{ "memos", E_SOURCE_EXTENSION_MEMO_LIST },
	{ "contacts", E_SOURCE_EXTENSION_ADDRESS_BOOK }
};

/* ------------------------------------------------------------------------ */
/* Fonctions internes partagees                                             */
/* ------------------------------------------------------------------------ */

/* evo-core-session.c */
CamelSession *
evo_core_session_new (EvoCore *core,
		      ESourceRegistry *registry);

/* evo-core.c */
gchar *
json_from_builder (JsonBuilder *builder);
gboolean
core_wait_started (EvoCore *core,
		   gchar **error_message);

/* evo-core-oauth2.c */
void
core_oauth2_pending_free (gpointer data);
void
core_check_pending_credentials (EvoCore *core);
void
core_credentials_required_cb (ESourceRegistry *registry,
			      ESource *source,
			      ESourceCredentialsReason reason,
			      const gchar *certificate_pem,
			      GTlsCertificateFlags certificate_errors,
			      const GError *op_error,
			      gpointer user_data);

/* evo-core-events.c */
void
core_emit_builder (EvoCore *core,
		   const gchar *event,
		   JsonBuilder *builder);
JsonBuilder *
core_event_builder (void);
void
builder_add_string (JsonBuilder *builder,
		    const gchar *name,
		    const gchar *value);
void
builder_add_boolean (JsonBuilder *builder,
		     const gchar *name,
		     gboolean value);
void
builder_add_int (JsonBuilder *builder,
		 const gchar *name,
		 gint64 value);
void
core_emit (EvoCore *core,
	   const gchar *event,
	   const gchar *message);
void
core_emit_progress (EvoCore *core,
		    const gchar *account,
		    const gchar *folder,
		    const gchar *message,
		    gint percent,
		    gboolean active);
void
core_emit_connection_status (EvoCore *core,
			     const gchar *account,
			     const gchar *service,
			     const gchar *status,
			     const gchar *message);
void
core_add_stores (EvoCore *core);

/* evo-core-requests.c */
void
core_request_run (gpointer data,
		  gpointer user_data);
void
core_ignore_result (const char *json,
		    const char *error_message,
		    void *user_data);
void
core_submit_named (EvoCore *core,
		   RequestFunc func,
		   const gchar *name,
		   gchar **args,
		   EvoCoreResultFunc result_func,
		   gpointer user_data);
/* Le nom de la fonction exécutante est conservé pour le journal des requêtes lentes
 * du démarrage (attente dans la file et durée d'exécution). */
#define core_submit(core, func, args, result_func, user_data) \
	core_submit_named ((core), (func), #func, (args), (result_func), (user_data))
CamelStore *
core_ref_store (EvoCore *core,
		const gchar *account_uid,
		GError **error);
gboolean
core_ensure_connected (EvoCore *core,
		       CamelStore *store,
		       GError **error);
gchar *
core_account_address (EvoCore *core,
		      const gchar *uid);

/* evo-core-accounts.c */
const gchar *
folder_type_name (CamelFolderInfoFlags flags);

/* evo-core-folders.c */
gint
compare_infos_by_date (gconstpointer a,
		       gconstpointer b);
void
core_folder_changed_cb (CamelFolder *folder,
			CamelFolderChangeInfo *changes,
			gpointer user_data);
gchar *
folder_key (const gchar *account_uid,
	    const gchar *folder_name);
CamelFolder *
core_ref_folder (EvoCore *core,
		 const gchar *account_uid,
		 const gchar *folder_name,
		 GError **error);
gchar *
core_find_role_folder (EvoCore *core,
		       CamelStore *store,
		       CamelFolderInfo *fi,
		       const gchar *role);

/* evo-core-messages.c */
gchar *
core_list_messages_run (EvoCore *core,
			gchar **args,
			GError **error);
gchar *
core_folder_background_refresh_run (EvoCore *core,
				    gchar **args,
				    GError **error);
gboolean
message_info_matches_query (CamelMessageInfo *info,
			    const gchar *query,
			    gchar **words);

/* evo-core-message-actions.c */
gchar *
find_trash_folder_name (CamelFolderInfo *fi);
CamelFolderInfo *
find_folder_info (CamelFolderInfo *fi,
		  const gchar *full_name);
guint32
find_folder_type (CamelFolderInfo *fi,
		  const gchar *full_name);

/* evo-core-folder-ops.c */
void
core_emit_folders_updated (EvoCore *core,
			   const gchar *account_uid);

/* evo-core-message-read.c */
gchar *
part_get_text (CamelMimePart *part,
	       GCancellable *cancellable);
gchar *
html_to_text (const gchar *html);
gchar *
sanitize_filename (const gchar *raw,
		   const gchar *fallback);
const gchar *
folder_info_find_inbox (CamelFolderInfo *fi);

/* evo-core-invitations.c */
void
core_invitation_describe (EvoCore *core,
			  const gchar *account_uid,
			  CamelFolder *folder,
			  const gchar *message_uid,
			  CamelMimeMessage *message,
			  JsonBuilder *builder);

/* evo-core-refresh.c */
gboolean
core_refresh_timeout_cb (gpointer user_data);
gboolean
parse_folder_uri (const gchar *uri,
		  gchar **out_store_uid,
		  gchar **out_folder_name);

/* evo-core-crypto.c : S/MIME natif Windows (CryptoAPI) */
typedef struct {
	gboolean encrypted;	/* le message était chiffré */
	gboolean is_signed;	/* le message est signé */
	gboolean valid;		/* signature intacte (et adresse concordante) / déchiffrement réussi */
	gboolean trusted;	/* certificat approuvé par Windows */
	gchar *signer;		/* « Nom <adresse> » du signataire */
	gchar *description;	/* explication en français */
} CoreCryptoInfo;

gboolean
core_crypto_protect_message (CamelMimeMessage *message,
			     const gchar *sender_email,
			     const gchar *sender_name,
			     gboolean sign,
			     gboolean encrypt,
			     CamelInternetAddress *recipients,
			     GError **error);
CamelMimePart *
core_crypto_open_pkcs7 (CamelMimePart *part,
			const gchar *from_email,
			CoreCryptoInfo *info);
void
core_crypto_verify_multipart_signed (CamelMultipart *signed_part,
				     const gchar *from_email,
				     CoreCryptoInfo *info);
void
core_crypto_info_clear (CoreCryptoInfo *info);

/* evo-core.c */
gboolean
core_startup_tracing (void);

/* evo-core-gal.c */
void
core_log_m365_identity (EvoCore *core,
			const gchar *display_name);
gchar *
core_photo_data_uri (gconstpointer data,
		     gsize size);
gchar *
core_gal_photo_by_domain (EvoCore *core,
			  const gchar *email,
			  gboolean *uncertain);

/* evo-core-contacts.c */
gchar *
core_contact_photo_by_email (EvoCore *core,
			     const gchar *email);

/* evo-core-send.c */
ESource *
core_ref_identity (EvoCore *core,
		   const gchar *account_uid);
void
core_process_outbox (EvoCore *core);

/* evo-core-account-setup.c */
gboolean
core_wait_for_sources (EvoCore *core,
		       GList *sources,
		       guint timeout_ms,
		       GError **error);
gboolean
core_store_source_password (EvoCore *core,
			    const gchar *source_uid,
			    const gchar *password,
			    GError **error);
void
core_set_m365_settings (ESource *source,
			const gchar *backend,
			const gchar *email,
			const gchar *user,
			const gchar *hosturl,
			const gchar *client_id,
			const gchar *tenant);

void
core_set_m365_impersonation (ESource *source,
			     const gchar *shared);

/* evo-core-account-cloud.c */
ESource *
core_ref_credentials_source (EvoCore *core,
			     const gchar *uid,
			     GError **error);

/* evo-core-detect.c */
void
core_set_google_mail_settings (ESource *source,
			       const gchar *backend,
			       const gchar *host,
			       guint16 port,
			       const gchar *email);

/* evo-core-sources.c */
gchar *
unique_file_path (const gchar *dir,
		  const gchar *name,
		  const gchar *ext);

#endif /* EVO_CORE_PRIVATE_H */
