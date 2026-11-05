/*
 * evo-core-test: console check of evo-core against the configured accounts.
 * Lists the accounts, the folders of the first network account and the most
 * recent messages of its inbox, then shows the first message.
 *
 * Plain C without GLib, like the user interface: only evo-core.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "evo-core.h"

static HANDLE done_event;
static char *last_json;
static char *last_error;

static void
on_event (const char *event,
	  const char *json,
	  void *user_data)
{
	SYSTEMTIME now;

	GetLocalTime (&now);
	printf ("[event %02d:%02d:%02d] %s %s\n", now.wHour, now.wMinute, now.wSecond, event, json);
	fflush (stdout);
}

static void
on_result (const char *json,
	   const char *error_message,
	   void *user_data)
{
	last_json = json ? _strdup (json) : NULL;
	last_error = error_message ? _strdup (error_message) : NULL;
	SetEvent (done_event);
}

static int
wait_result (const char *what)
{
	WaitForSingleObject (done_event, INFINITE);

	if (last_error) {
		printf ("[%s] ERROR: %s\n", what, last_error);
		return 0;
	}

	printf ("[%s] %.30000s%s\n\n", what, last_json, strlen (last_json) > 30000 ? " ..." : "");
	fflush (stdout);

	return 1;
}

/* value of the first "key":"value" after start, very small JSON helper */
static char *
json_first_string (const char *json,
		   const char *key,
		   const char *after)
{
	char pattern[64], *start, *end, *value;

	snprintf (pattern, sizeof (pattern), "\"%s\":\"", key);
	start = strstr (after ? after : json, pattern);
	if (!start)
		return NULL;

	start += strlen (pattern);
	end = strchr (start, '"');
	if (!end)
		return NULL;

	value = malloc (end - start + 1);
	memcpy (value, start, end - start);
	value[end - start] = '\0';

	return value;
}

/* number of objects in a JSON array, roughly: count of "uid" members */
static int
count_uids (const char *json)
{
	int count = 0;
	const char *pos = json;

	while (pos && (pos = strstr (pos, "\"uid\":")) != NULL) {
		count++;
		pos++;
	}

	return count;
}

/* evo-core-test --watch [minutes]: lists the inbox every 20 s, to check
 * that new mail is seen without restarting */
static int
watch_inbox (EvoCore *core,
	     const char *account,
	     int minutes)
{
	int round;

	for (round = 0; round < minutes * 3; round++) {
		SYSTEMTIME now;

		evo_core_refresh (core);
		Sleep (5000);

		evo_core_list_messages (core, account, "INBOX", 0, on_result, NULL);
		WaitForSingleObject (done_event, INFINITE);

		GetLocalTime (&now);
		if (last_error)
			printf ("[watch %02d:%02d:%02d] ERROR: %s\n", now.wHour, now.wMinute, now.wSecond, last_error);
		else
			printf ("[watch %02d:%02d:%02d] INBOX: %d messages\n", now.wHour, now.wMinute, now.wSecond, count_uids (last_json));
		fflush (stdout);

		Sleep (15000);
	}

	return 0;
}

int
main (int argc,
      char **argv)
{
	EvoCore *core;
	char *account = NULL, *message_uid = NULL;
	int watch_minutes = 0;

	/* --autoreply compte : réponses automatiques lues sur le serveur (octets du message interne) */
	if (argc > 2 && strcmp (argv[1], "--autoreply") == 0) {
		char *pos;

		done_event = CreateEvent (NULL, FALSE, FALSE, NULL);
		core = evo_core_new (on_event, NULL);
		evo_core_get_auto_reply (core, argv[2], on_result, NULL);
		WaitForSingleObject (done_event, INFINITE);
		if (last_error)
			printf ("[autoreply] ERREUR : %s\n", last_error);
		else if ((pos = strstr (last_json, "internal_message")) != NULL) {
			int ii;

			pos += 20;
			printf ("[autoreply] octets : ");
			for (ii = 0; ii < 1400 && pos[ii]; ii++)
				printf ("%02x ", (unsigned char) pos[ii]);
			printf ("\n");
		}
		evo_core_free (core);
		return 0;
	}

	/* --nav : groupes de calendriers d'Outlook lus par EWS */
	if (argc > 1 && strcmp (argv[1], "--nav") == 0) {
		done_event = CreateEvent (NULL, FALSE, FALSE, NULL);
		core = evo_core_new (on_event, NULL);
		evo_core_list_nav_calendars (core, "", on_result, NULL);
		wait_result ("groupes de calendriers EWS");
		evo_core_free (core);
		return 0;
	}

	/* --photo adresse : photo de profil de l'annuaire (taille de la réponse) */
	if (argc > 2 && strcmp (argv[1], "--photo") == 0) {
		done_event = CreateEvent (NULL, FALSE, FALSE, NULL);
		core = evo_core_new (on_event, NULL);
		evo_core_get_gal_photo (core, "", argv[2], on_result, NULL);
		WaitForSingleObject (done_event, INFINITE);
		printf ("[photo] %s, %u octets\n", last_error ? last_error : "ok", last_json ? (unsigned) strlen (last_json) : 0);
		evo_core_free (core);
		return 0;
	}

	/* --people [adresse] : équipe de l'utilisateur, puis agenda d'une personne (14 jours) */
	if (argc > 1 && strcmp (argv[1], "--people") == 0) {
		char *email = argc > 2 ? _strdup (argv[2]) : NULL;
		time_t now = time (NULL);

		done_event = CreateEvent (NULL, FALSE, FALSE, NULL);
		core = evo_core_new (on_event, NULL);
		evo_core_list_team (core, "", on_result, NULL);
		if (wait_result ("equipe") && !email)
			email = json_first_string (last_json, "email", NULL);
		if (email) {
			evo_core_list_person_events (core, "", email, email, "#3498db",
				(long long) now, (long long) now + 14 * 24 * 3600, on_result, NULL);
			wait_result ("calendrier de la personne");
		}
		evo_core_free (core);
		return 0;
	}

	/* --calendars : liste les agendas, force la redécouverte auprès des serveurs
	 * (Exchange Online, Google...) puis les liste de nouveau */
	if (argc > 1 && strcmp (argv[1], "--calendars") == 0) {
		done_event = CreateEvent (NULL, FALSE, FALSE, NULL);
		core = evo_core_new (on_event, NULL);
		evo_core_list_sources (core, "calendar", on_result, NULL);
		wait_result ("calendars (avant)");
		evo_core_refresh_calendars (core, on_result, NULL);
		wait_result ("refresh");
		Sleep (30000);
		evo_core_list_sources (core, "calendar", on_result, NULL);
		wait_result ("calendars (apres)");
		evo_core_free (core);
		return 0;
	}

	if (argc > 1 && strcmp (argv[1], "--watch") == 0)
		watch_minutes = argc > 2 ? atoi (argv[2]) : 5;

	/* do not mark the shown message as read */
	SetEnvironmentVariableA ("EVO_CORE_READ_ONLY", "1");
	_putenv ("EVO_CORE_READ_ONLY=1");

	done_event = CreateEvent (NULL, FALSE, FALSE, NULL);
	core = evo_core_new (on_event, NULL);

	evo_core_list_accounts (core, on_result, NULL);
	if (wait_result ("accounts")) {
		/* first network account */
		char *pos = strstr (last_json, "\"local\":false");
		if (pos) {
			char *obj = pos;
			while (obj > last_json && *obj != '{')
				obj--;
			account = json_first_string (last_json, "uid", obj);
		}
	}

	if (account && watch_minutes > 0) {
		watch_inbox (core, account, watch_minutes);
	} else if (account) {
		evo_core_list_folders (core, account, on_result, NULL);
		wait_result ("folders");

		evo_core_list_messages (core, account, "INBOX", 5, on_result, NULL);
		if (wait_result ("messages"))
			message_uid = json_first_string (last_json, "uid", NULL);

		if (message_uid) {
			evo_core_get_message (core, account, "INBOX", message_uid, on_result, NULL);
			wait_result ("message");
		}
	} else {
		printf ("no network account\n");
	}

	evo_core_free (core);

	return 0;
}
