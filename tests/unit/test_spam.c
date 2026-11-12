#define _CRT_SECURE_NO_WARNINGS 1
/*
 * Tests unitaires du classificateur bayésien anti-spam (evo-core-spam-util.h) :
 * découpage en jetons, apprentissage, score, suivi des messages appris,
 * persistance et élagage. Logique pure : aucune dépendance à GLib ni à Camel.
 */

#include <stdio.h>
#include <string.h>

#include "../../winui/core/evo-core-spam-util.h"

static int tests_run = 0;
static int tests_failed = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			printf ("    ECHEC ligne %d : %s\n", __LINE__, #cond); \
			failed = 1; \
		} \
	} while (0)

/**
 * @brief Enregistre le résultat d'un test et l'affiche.
 * @param name nom du test
 * @param failed 0 si réussi
 */
static void
report (const char *name,
	int failed)
{
	tests_run++;
	if (failed)
		tests_failed++;
	printf ("  [%s] %s\n", failed ? "ECHEC" : "OK", name);
}

/**
 * @brief Construit l'ensemble de jetons d'un texte de corps.
 */
static void
tokens_of (SpamDb *set,
	   const char *text)
{
	spam_db_init (set);
	spam_tokenize (set, text, NULL);
}

static const char *SPAM_TEXTS[] = {
	"Buy cheap viagra pills now!!! Free money, click here to win a prize",
	"Congratulations winner! Claim your free prize money today, click now",
	"Cheap pills discount viagra online pharmacy, order now and save",
	"URGENT: your account is suspended, click here to verify your password now",
	"Earn money fast, work from home, free casino bonus, click here",
};

static const char *HAM_TEXTS[] = {
	"Hello, please find the agenda for tomorrow's project meeting attached",
	"Minutes of the weekly team meeting and the planning for the next sprint",
	"Could you review the design document before the meeting on Thursday?",
	"Thanks for the report, the budget figures look consistent with the plan",
	"Lunch with the team after the presentation, let me know if you can join",
};

/**
 * @brief Entraîne une base avec 4 fois chaque série de textes (20 messages par catégorie).
 */
static void
train_sample (SpamDb *db)
{
	int round, ii;
	char key[40];

	spam_db_init (db);
	for (round = 0; round < 4; round++) {
		for (ii = 0; ii < 5; ii++) {
			SpamDb set;
			char text[256];

			snprintf (text, sizeof (text), "%s ref%d%c", SPAM_TEXTS[ii], round, 'a');
			tokens_of (&set, text);
			snprintf (key, sizeof (key), "@spam%d_%d", round, ii);
			spam_db_train (db, &set, key, 1);
			spam_db_clear (&set);

			snprintf (text, sizeof (text), "%s ref%d%c", HAM_TEXTS[ii], round, 'b');
			tokens_of (&set, text);
			snprintf (key, sizeof (key), "@ham%d_%d", round, ii);
			spam_db_train (db, &set, key, 0);
			spam_db_clear (&set);
		}
	}
}

static void
test_tokenize_basics (void)
{
	int failed = 0;
	SpamDb set;

	tokens_of (&set, "Bonjour, VIAGRA viagra à 12345 euros; notre offre-spéciale du jour!");
	CHECK (spam_db_lookup (&set, "bonjour") != NULL);
	CHECK (spam_db_lookup (&set, "viagra") != NULL);
	CHECK (set.used >= 4);
	/* dédoublonnage : « VIAGRA viagra » ne donne qu'un jeton */
	{
		size_t ii, viagra = 0;
		for (ii = 0; ii < set.capacity; ii++)
			if (set.entries[ii].token && strcmp (set.entries[ii].token, "viagra") == 0)
				viagra++;
		CHECK (viagra == 1);
	}
	CHECK (spam_db_lookup (&set, "12345") == NULL);	/* nombre seul ignoré */
	CHECK (spam_db_lookup (&set, "à") == NULL);	/* trop court */
	CHECK (spam_db_lookup (&set, "euros") != NULL);
	CHECK (spam_db_lookup (&set, "offre-spéciale") != NULL);
	spam_db_clear (&set);

	tokens_of (&set, NULL);
	CHECK (set.used == 0);
	spam_db_clear (&set);

	report ("tokenisation : minuscules, doublons, nombres et mots courts ignores", failed);
}

static void
test_tokenize_accents_and_limits (void)
{
	int failed = 0;
	SpamDb set;
	char longword[100];

	tokens_of (&set, "ÉLÉPHANT Éléphant été");
	CHECK (spam_db_lookup (&set, "éléphant") != NULL);
	CHECK (set.used == 2);	/* « éléphant » et « été » */
	spam_db_clear (&set);

	memset (longword, 'a', sizeof (longword) - 1);
	longword[sizeof (longword) - 1] = '\0';
	tokens_of (&set, longword);
	CHECK (set.used == 0);	/* trop long : ignoré */
	spam_db_clear (&set);

	spam_db_init (&set);
	spam_tokenize (&set, "Offre exclusive", "s:");
	CHECK (spam_db_lookup (&set, "s:offre") != NULL);
	CHECK (spam_db_lookup (&set, "offre") == NULL);
	spam_db_clear (&set);

	tokens_of (&set, "Gagnez maintenant !!! vite");
	CHECK (spam_db_lookup (&set, "*excl") != NULL);
	spam_db_clear (&set);
	tokens_of (&set, "Gagnez maintenant ! vite");
	CHECK (spam_db_lookup (&set, "*excl") == NULL);
	spam_db_clear (&set);

	report ("tokenisation : accents replies, mot trop long, prefixe, exclamations", failed);
}

static void
test_tokenize_address (void)
{
	int failed = 0;
	SpamDb set;

	spam_db_init (&set);
	spam_tokenize_address (&set, "Promo Deals <Offers@Shop-Example.COM>", "f:");
	CHECK (spam_db_lookup (&set, "f:promo") != NULL);
	CHECK (spam_db_lookup (&set, "d:f:shop-example.com") != NULL);
	spam_db_clear (&set);

	spam_db_init (&set);
	spam_tokenize_address (&set, "sans-arobase", "f:");
	CHECK (spam_db_lookup (&set, "f:sans-arobase") != NULL);
	spam_db_clear (&set);

	report ("tokenisation : adresse et domaine de l'expediteur", failed);
}

static void
test_score_untrained (void)
{
	int failed = 0;
	SpamDb db, set;
	int significant = -1;

	spam_db_init (&db);
	tokens_of (&set, "Buy cheap viagra now");
	CHECK (spam_score (&db, &set, &significant) == 0.5);
	CHECK (significant == 0);
	CHECK (!spam_db_ready (&db));
	spam_db_clear (&set);
	spam_db_clear (&db);

	report ("score : base vide = 0,5 et filtre non pret", failed);
}

static void
test_score_trained (void)
{
	int failed = 0;
	SpamDb db, set;
	int significant = 0;
	double spam_score_value, ham_score_value, neutral;

	train_sample (&db);
	CHECK (db.n_spam == 20 && db.n_ham == 20);
	CHECK (spam_db_ready (&db));

	tokens_of (&set, "Free viagra pills, click here to claim your prize money now");
	spam_score_value = spam_score (&db, &set, &significant);
	CHECK (spam_score_value > 0.95);
	CHECK (significant > 3);
	spam_db_clear (&set);

	tokens_of (&set, "Please send the meeting agenda and the project budget plan");
	ham_score_value = spam_score (&db, &set, NULL);
	CHECK (ham_score_value < 0.05);
	spam_db_clear (&set);

	tokens_of (&set, "zebra quantum harmonica volcano");
	neutral = spam_score (&db, &set, NULL);
	CHECK (neutral == 0.5);
	spam_db_clear (&set);

	/* message mixte : ni franchement indésirable ni franchement légitime */
	tokens_of (&set, "meeting viagra");
	neutral = spam_score (&db, &set, NULL);
	CHECK (neutral > 0.2 && neutral < 0.8);
	spam_db_clear (&set);

	spam_db_clear (&db);
	report ("score : indesirable proche de 1, legitime proche de 0, inconnu 0,5", failed);
}

static void
test_ready_threshold (void)
{
	int failed = 0;
	SpamDb db, set;
	int ii;
	char key[32];

	spam_db_init (&db);
	tokens_of (&set, "alpha bravo charlie");
	for (ii = 0; ii < 30; ii++) {
		snprintf (key, sizeof (key), "@s%d", ii);
		spam_db_train (&db, &set, key, 1);
	}
	CHECK (db.n_spam == 30 && db.n_ham == 0);
	CHECK (!spam_db_ready (&db));	/* aucun message légitime : pas fiable */
	for (ii = 0; ii < SPAM_MIN_TRAINED - 1; ii++) {
		snprintf (key, sizeof (key), "@h%d", ii);
		spam_db_train (&db, &set, key, 0);
	}
	CHECK (!spam_db_ready (&db));
	spam_db_train (&db, &set, "@hlast", 0);
	CHECK (spam_db_ready (&db));
	spam_db_clear (&set);
	spam_db_clear (&db);

	report ("pret seulement avec le minimum de messages dans chaque categorie", failed);
}

static void
test_train_idempotent_and_switch (void)
{
	int failed = 0;
	SpamDb db, set;
	SpamEntry *entry;

	spam_db_init (&db);
	tokens_of (&set, "promotion exclusive");

	CHECK (spam_db_train (&db, &set, "@m1", 1) == 1);
	CHECK (spam_db_train (&db, &set, "@m1", 1) == 0);	/* déjà appris */
	CHECK (db.n_spam == 1 && db.n_ham == 0);
	entry = spam_db_lookup (&db, "promotion");
	CHECK (entry && entry->spam == 1 && entry->ham == 0);

	/* l'utilisateur change d'avis : « Non indésirable » corrige l'apprentissage */
	CHECK (spam_db_train (&db, &set, "@m1", 0) == 1);
	CHECK (db.n_spam == 0 && db.n_ham == 1);
	entry = spam_db_lookup (&db, "promotion");
	CHECK (entry && entry->spam == 0 && entry->ham == 1);
	entry = spam_db_lookup (&db, "@m1");
	CHECK (entry && entry->spam == 0 && entry->ham == 1);

	/* sans clé de suivi : comptage à chaque appel */
	CHECK (spam_db_train (&db, &set, NULL, 1) == 1);
	CHECK (spam_db_train (&db, &set, NULL, 1) == 1);
	CHECK (db.n_spam == 2);

	spam_db_clear (&set);
	spam_db_clear (&db);

	report ("apprentissage : pas de double comptage, correction de categorie", failed);
}

static void
test_message_key_and_token_count (void)
{
	int failed = 0;
	char a[32], b[32], c[32];
	SpamDb db, set;

	spam_message_key ("<id1@exemple.org>", a, sizeof (a));
	spam_message_key ("<id1@exemple.org>", b, sizeof (b));
	spam_message_key ("<id2@exemple.org>", c, sizeof (c));
	CHECK (a[0] == '@' && strlen (a) == 17);
	CHECK (strcmp (a, b) == 0);
	CHECK (strcmp (a, c) != 0);

	spam_db_init (&db);
	tokens_of (&set, "alpha bravo");
	spam_db_train (&db, &set, a, 1);
	CHECK (spam_db_token_count (&db) == 2);	/* la clé de suivi n'est pas un jeton */
	CHECK (db.used == 3);
	spam_db_clear (&set);
	spam_db_clear (&db);

	report ("cle de suivi stable, exclue du nombre de jetons", failed);
}

static void
test_persistence_roundtrip (void)
{
	int failed = 0;
	SpamDb db, loaded, set;
	FILE *file;
	double before, after;

	train_sample (&db);
	tokens_of (&set, "Free viagra pills, click here to claim your prize money now");
	before = spam_score (&db, &set, NULL);

	file = tmpfile ();
	CHECK (file != NULL);
	if (file) {
		CHECK (spam_db_write (&db, file) == 0);
		rewind (file);
		spam_db_init (&loaded);
		CHECK (spam_db_read (&loaded, file) == 0);
		CHECK (loaded.n_spam == db.n_spam && loaded.n_ham == db.n_ham);
		CHECK (loaded.used == db.used);
		after = spam_score (&loaded, &set, NULL);
		CHECK (fabs (before - after) < 1e-12);
		/* le suivi des messages survit à l'enregistrement */
		CHECK (spam_db_lookup (&loaded, "@spam0_0") != NULL);
		spam_db_clear (&loaded);
		fclose (file);
	}

	spam_db_clear (&set);
	spam_db_clear (&db);

	report ("persistance : ecriture puis relecture donnent le meme score", failed);
}

static void
test_persistence_robustness (void)
{
	int failed = 0;
	SpamDb db;
	FILE *file;

	file = tmpfile ();
	CHECK (file != NULL);
	if (file) {
		fputs ("pas une base\n1 2 x\n", file);
		rewind (file);
		spam_db_init (&db);
		CHECK (spam_db_read (&db, file) == -1);
		CHECK (db.used == 0 && db.n_spam == 0);
		fclose (file);
	}

	file = tmpfile ();
	if (file) {
		fputs (SPAM_DB_MAGIC "\nN 5 7\n3 1 bon\nligne cassee\n2 x\n0 4 autre\n", file);
		rewind (file);
		spam_db_init (&db);
		CHECK (spam_db_read (&db, file) == 0);
		CHECK (db.n_spam == 5 && db.n_ham == 7);
		CHECK (spam_db_lookup (&db, "bon") != NULL && spam_db_lookup (&db, "bon")->spam == 3);
		CHECK (spam_db_lookup (&db, "autre") != NULL && spam_db_lookup (&db, "autre")->ham == 4);
		CHECK (db.used == 2);	/* lignes mal formées ignorées */
		spam_db_clear (&db);
		fclose (file);
	}

	file = tmpfile ();
	if (file) {
		spam_db_init (&db);
		CHECK (spam_db_read (&db, file) == -1);	/* fichier vide */
		fclose (file);
	}

	report ("persistance : fichier invalide ou partiellement corrompu", failed);
}

static void
test_prune (void)
{
	int failed = 0;
	SpamDb db;
	size_t ii, removed;
	char token[40];
	SpamEntry *entry;

	spam_db_init (&db);
	db.n_spam = 3;
	db.n_ham = 3;
	entry = spam_db_add (&db, "frequent");
	entry->spam = 5;
	entry = spam_db_add (&db, "@marque");
	entry->ham = 1;

	/* sous le seuil : rien n'est supprimé */
	CHECK (spam_db_prune (&db) == 0);

	for (ii = 0; ii < SPAM_MAX_ENTRIES + 10; ii++) {
		snprintf (token, sizeof (token), "rare%zu", ii);
		entry = spam_db_add (&db, token);
		entry->ham = 1;
	}
	removed = spam_db_prune (&db);
	CHECK (removed == SPAM_MAX_ENTRIES + 10);
	CHECK (spam_db_lookup (&db, "frequent") != NULL);
	CHECK (spam_db_lookup (&db, "@marque") != NULL);
	CHECK (spam_db_lookup (&db, "rare5") == NULL);
	CHECK (db.n_spam == 3 && db.n_ham == 3);

	spam_db_clear (&db);

	report ("elagage : jetons vus une fois supprimes au-dela du seuil", failed);
}

static void
test_reset (void)
{
	int failed = 0;
	SpamDb db;

	train_sample (&db);
	CHECK (db.used > 0);
	spam_db_clear (&db);
	CHECK (db.used == 0 && db.n_spam == 0 && db.n_ham == 0 && db.entries == NULL);
	CHECK (!spam_db_ready (&db));
	/* la base reste utilisable après remise à zéro */
	{
		SpamDb set;
		tokens_of (&set, "retour apres reset");
		CHECK (spam_db_train (&db, &set, "@x", 1) == 1);
		CHECK (db.n_spam == 1);
		spam_db_clear (&set);
	}
	spam_db_clear (&db);

	report ("remise a zero de l'apprentissage", failed);
}

int
main (void)
{
	printf ("\n=== Tests du classificateur bayesien anti-spam ===\n");

	test_tokenize_basics ();
	test_tokenize_accents_and_limits ();
	test_tokenize_address ();
	test_score_untrained ();
	test_score_trained ();
	test_ready_threshold ();
	test_train_idempotent_and_switch ();
	test_message_key_and_token_count ();
	test_persistence_roundtrip ();
	test_persistence_robustness ();
	test_prune ();
	test_reset ();

	printf ("=== %d tests, %d echec(s) ===\n", tests_run, tests_failed);

	return tests_failed ? 1 : 0;
}
