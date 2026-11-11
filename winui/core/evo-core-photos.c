/*
 * evo-core : Photos des correspondants (expéditeurs) avec cache disque.
 * Voir evo-core.h et evo-core-private.h.
 *
 * Ordre de recherche : annuaire du compte dont le domaine est celui de l'adresse
 * (Microsoft 365 ou Google Workspace), puis carnets d'adresses personnels ; sans
 * photo, l'interface affiche les initiales du nom.
 */

#include "evo-core-private.h"

#include <glib/gstdio.h>

/* durée de vie d'une photo en cache (une semaine au plus) */
#define PHOTO_TTL_FOUND (7 * 24 * 3600)
/* durée de vie d'un « pas de photo » : une photo ajoutée plus tard apparaît le lendemain */
#define PHOTO_TTL_NONE (24 * 3600)
/* intervalle minimal entre deux nettoyages du dossier de cache */
#define PHOTO_PRUNE_INTERVAL (3600)

/* Dossier du cache disque des photos (créé si besoin) ; NULL si indisponible.
 * Retourne : chemin (g_free). */
static gchar *
photo_cache_dir (void)
{
	const gchar *local_app_data = g_getenv ("LOCALAPPDATA");
	gchar *dir;

	if (!local_app_data || !*local_app_data)
		return NULL;

	dir = g_build_filename (local_app_data, "EvolutionWin", "photos", NULL);
	if (g_mkdir_with_parents (dir, 0700) != 0) {
		g_free (dir);
		return NULL;
	}

	return dir;
}

/* Chemin d'un fichier du cache pour une adresse (empreinte SHA-1 de l'adresse).
 * Paramètres :
 *   dir   : dossier du cache
 *   email : adresse en minuscules
 *   ext   : extension : "photo" (URI de données) ou "none" (aucune photo)
 * Retourne : chemin (g_free). */
static gchar *
photo_cache_path (const gchar *dir,
		  const gchar *email,
		  const gchar *ext)
{
	gchar *key = g_compute_checksum_for_string (G_CHECKSUM_SHA1, email, -1);
	gchar *name = g_strconcat (key, ".", ext, NULL);
	gchar *path = g_build_filename (dir, name, NULL);

	g_free (key);
	g_free (name);

	return path;
}

/* Âge d'un fichier en secondes ; -1 s'il n'existe pas. */
static gint64
photo_file_age (const gchar *path)
{
	GStatBuf st;

	if (g_stat (path, &st) != 0)
		return -1;

	return MAX ((gint64) 0, g_get_real_time () / G_USEC_PER_SEC - (gint64) st.st_mtime);
}

/* Supprime du cache les fichiers plus vieux que la durée de vie maximale (au plus
 * une fois par heure).
 * Paramètres :
 *   dir : dossier du cache */
static void
photo_cache_prune (const gchar *dir)
{
	static gint64 last_prune = 0;
	gint64 now = g_get_real_time () / G_USEC_PER_SEC;
	GDir *handle;
	const gchar *name;

	if (now - last_prune < PHOTO_PRUNE_INTERVAL)
		return;
	last_prune = now;

	handle = g_dir_open (dir, 0, NULL);
	if (!handle)
		return;

	while ((name = g_dir_read_name (handle))) {
		gchar *path = g_build_filename (dir, name, NULL);

		if (photo_file_age (path) > PHOTO_TTL_FOUND)
			g_remove (path);
		g_free (path);
	}
	g_dir_close (handle);
}

/* Lit le cache pour une adresse.
 * Paramètres :
 *   dir   : dossier du cache
 *   email : adresse en minuscules
 *   photo : reçoit l'URI de données (g_free) ou NULL pour « aucune photo »
 * Retourne : TRUE si une entrée valide existe (photo ou absence de photo). */
static gboolean
photo_cache_read (const gchar *dir,
		  const gchar *email,
		  gchar **photo)
{
	gchar *found = photo_cache_path (dir, email, "photo");
	gchar *none = photo_cache_path (dir, email, "none");
	gint64 age;
	gboolean hit = FALSE;

	*photo = NULL;

	age = photo_file_age (found);
	if (age >= 0 && age < PHOTO_TTL_FOUND) {
		gchar *contents = NULL;

		if (g_file_get_contents (found, &contents, NULL, NULL) && contents && *contents) {
			*photo = contents;
			hit = TRUE;
		} else {
			g_free (contents);
		}
	} else if (age >= 0) {
		g_remove (found);
	}

	if (!hit) {
		age = photo_file_age (none);
		if (age >= 0 && age < PHOTO_TTL_NONE)
			hit = TRUE;
		else if (age >= 0)
			g_remove (none);
	}

	g_free (found);
	g_free (none);

	return hit;
}

/* Écrit le résultat d'une recherche dans le cache.
 * Paramètres :
 *   dir   : dossier du cache
 *   email : adresse en minuscules
 *   photo : URI de données, ou NULL pour mémoriser « aucune photo » */
static void
photo_cache_write (const gchar *dir,
		   const gchar *email,
		   const gchar *photo)
{
	gchar *found = photo_cache_path (dir, email, "photo");
	gchar *none = photo_cache_path (dir, email, "none");

	if (photo) {
		g_file_set_contents (found, photo, -1, NULL);
		g_remove (none);
	} else {
		g_file_set_contents (none, "", 0, NULL);
		g_remove (found);
	}

	g_free (found);
	g_free (none);
}

/* Photo d'un correspondant : cache, annuaire du domaine, contacts personnels.
 * Paramètres :
 *   core  : instance du moteur evo-core
 *   args  : [0] = adresse électronique
 *   error : emplacement pour une éventuelle erreur
 * Retourne : {"photo": "data:image/...;base64,..."} ; "photo" vide si aucune. */
static gchar *
core_sender_photo_run (EvoCore *core,
		       gchar **args,
		       GError **error)
{
	gchar *email = args[0] ? g_ascii_strdown (g_strstrip (args[0]), -1) : g_strdup ("");
	gchar *dir = NULL, *photo = NULL, *json;
	gboolean cached = FALSE, uncertain = FALSE;
	JsonBuilder *builder;

	if (strchr (email, '@')) {
		dir = photo_cache_dir ();
		if (dir) {
			photo_cache_prune (dir);
			cached = photo_cache_read (dir, email, &photo);
		}

		if (!cached) {
			/* annuaire du compte dont le domaine est celui de l'adresse, puis carnets personnels */
			photo = core_gal_photo_by_domain (core, email, &uncertain);
			if (!photo)
				photo = core_contact_photo_by_email (core, email);

			/* un échec réseau ou de droits ne doit pas être mémorisé comme « aucune photo » */
			if (dir && (photo || !uncertain))
				photo_cache_write (dir, email, photo);
		}
	}

	builder = json_builder_new ();
	json_builder_begin_object (builder);
	builder_add_string (builder, "photo", photo ? photo : "");
	json_builder_end_object (builder);
	json = json_from_builder (builder);
	g_object_unref (builder);

	g_free (photo);
	g_free (dir);
	g_free (email);

	return json;
}

/* Soumet la recherche de la photo d'un correspondant (voir evo_core_get_sender_photo
 * dans evo-core.h).
 * Paramètres :
 *   core      : instance du moteur evo-core
 *   email     : adresse de la personne
 *   func      : rappel recevant {"photo": "data:image/...;base64,..."} (vide si aucune)
 *   user_data : données utilisateur transmises au rappel */
void
evo_core_get_sender_photo (EvoCore *core,
			   const char *email,
			   EvoCoreResultFunc func,
			   void *user_data)
{
	gchar *args[] = { (gchar *) (email ? email : ""), NULL };

	g_return_if_fail (core != NULL);

	core_submit (core, core_sender_photo_run, g_strdupv (args), func, user_data);
}
