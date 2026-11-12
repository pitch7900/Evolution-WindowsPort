/**
 * @file test_offline_queue.c
 * @brief Tests unitaires de la file d'attente d'actions hors ligne (evo-core-offline-queue-util.h)
 * et de la detection de conflits calendrier / contacts (evo-core-conflict-util.h).
 *
 * Fonctions pures (sans GLib) : fusion des actions redondantes, classement des erreurs de
 * rejeu, politique de nouvelle tentative, jeton de revision et arbitrage des conflits.
 */

#include "../include/test_framework.h"
#include "../../winui/core/evo-core-offline-queue-util.h"
#include "../../winui/core/evo-core-conflict-util.h"

/** Fabrique une action de test. */
static OfflineAction make_action(int kind, const char *uid, const char *flag, int value)
{
    OfflineAction a;
    memset(&a, 0, sizeof(a));
    a.kind = kind;
    oq_copy(a.account, sizeof(a.account), "acc1");
    oq_copy(a.folder, sizeof(a.folder), "INBOX");
    oq_copy(a.uid, sizeof(a.uid), uid);
    oq_copy(a.flag, sizeof(a.flag), flag);
    a.value = value;
    return a;
}

static OfflineAction g_queue[OQ_MAX_ACTIONS];
static int g_count;

/** Ajoute une action avec un identifiant croissant. */
static int add(OfflineAction a)
{
    a.id = oq_next_id(g_queue, g_count, 0);
    return oq_queue_add(g_queue, &g_count, &a);
}

static void test_queue_basics(void)
{
    TEST_CASE("File hors ligne: noms de types et copie bornee");
    {
        char small[4];
        ASSERT_EQ_STR("delete", oq_kind_name(OQ_KIND_DELETE));
        ASSERT_EQ_STR("flag", oq_kind_name(OQ_KIND_FLAG));
        ASSERT_EQ_INT(OQ_KIND_MOVE, oq_kind_from_name("move"));
        ASSERT_EQ_INT(OQ_KIND_NONE, oq_kind_from_name("autre"));
        ASSERT_EQ_INT(OQ_KIND_NONE, oq_kind_from_name(NULL));
        oq_copy(small, sizeof(small), "abcdefgh");
        ASSERT_EQ_STR("abc", small);
        oq_copy(small, sizeof(small), NULL);
        ASSERT_EQ_STR("", small);
    }
    TEST_CASE_END();

    TEST_CASE("File hors ligne: ajout en ordre et identifiants croissants");
    {
        g_count = 0;
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_DELETE, "1", "", 0)));
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_DELETE, "2", "", 0)));
        ASSERT_EQ_INT(2, g_count);
        ASSERT_TRUE(g_queue[0].id < g_queue[1].id);
        ASSERT_EQ_INT(1, oq_queue_find(g_queue, g_count, g_queue[1].id));
        ASSERT_EQ_INT(-1, oq_queue_find(g_queue, g_count, 999));
        ASSERT_EQ_INT(2, oq_count_pending(g_queue, g_count));
        oq_queue_remove_at(g_queue, &g_count, 0);
        ASSERT_EQ_INT(1, g_count);
        ASSERT_EQ_STR("2", g_queue[0].uid);
        ASSERT_TRUE(oq_next_id(g_queue, g_count, 10) == 11);
    }
    TEST_CASE_END();
}

static void test_queue_coalescing(void)
{
    TEST_CASE("File hors ligne: deux changements du meme indicateur fusionnes (derniere valeur)");
    {
        g_count = 0;
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_FLAG, "1", "seen", 1)));
        ASSERT_EQ_INT(OQ_ADD_MERGED, add(make_action(OQ_KIND_FLAG, "1", "seen", 0)));
        ASSERT_EQ_INT(1, g_count);
        ASSERT_EQ_INT(0, g_queue[0].value);
        /* indicateur different ou autre message : actions distinctes */
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_FLAG, "1", "flagged", 1)));
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_FLAG, "2", "seen", 1)));
        ASSERT_EQ_INT(3, g_count);
    }
    TEST_CASE_END();

    TEST_CASE("File hors ligne: la suppression annule les indicateurs en attente et ferme le message");
    {
        g_count = 0;
        add(make_action(OQ_KIND_FLAG, "1", "seen", 1));
        add(make_action(OQ_KIND_FLAG, "1", "flagged", 1));
        add(make_action(OQ_KIND_FLAG, "2", "seen", 1));
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_DELETE, "1", "", 0)));
        ASSERT_EQ_INT(2, g_count);
        ASSERT_EQ_STR("2", g_queue[0].uid);
        ASSERT_EQ_INT(OQ_KIND_DELETE, g_queue[1].kind);
        /* le message supprime n'accepte plus d'action */
        ASSERT_EQ_INT(OQ_ADD_DROPPED, add(make_action(OQ_KIND_FLAG, "1", "seen", 0)));
        ASSERT_EQ_INT(OQ_ADD_DROPPED, add(make_action(OQ_KIND_DELETE, "1", "", 0)));
        ASSERT_EQ_INT(2, g_count);
        ASSERT_TRUE(oq_is_message_pending_removal(g_queue, g_count, "acc1", "INBOX", "1"));
        ASSERT_FALSE(oq_is_message_pending_removal(g_queue, g_count, "acc1", "INBOX", "2"));
        ASSERT_FALSE(oq_is_message_pending_removal(g_queue, g_count, "acc2", "INBOX", "1"));
    }
    TEST_CASE_END();

    TEST_CASE("File hors ligne: deplacement ignore si destination = origine, sinon masque le message");
    {
        OfflineAction mv = make_action(OQ_KIND_MOVE, "7", "", 0);
        g_count = 0;
        oq_copy(mv.dest_account, sizeof(mv.dest_account), "acc1");
        oq_copy(mv.dest_folder, sizeof(mv.dest_folder), "INBOX");
        ASSERT_EQ_INT(OQ_ADD_DROPPED, add(mv));
        oq_copy(mv.dest_folder, sizeof(mv.dest_folder), "Archive");
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(mv));
        ASSERT_TRUE(oq_is_message_pending_removal(g_queue, g_count, "acc1", "INBOX", "7"));
        /* une copie ne retire pas le message */
        g_count = 0;
        mv.kind = OQ_KIND_COPY;
        ASSERT_EQ_INT(OQ_ADD_ADDED, add(mv));
        ASSERT_FALSE(oq_is_message_pending_removal(g_queue, g_count, "acc1", "INBOX", "7"));
        ASSERT_EQ_INT(OQ_ADD_DROPPED, add(make_action(OQ_KIND_NONE, "7", "", 0)));
    }
    TEST_CASE_END();

    TEST_CASE("File hors ligne: file pleine refusee");
    {
        int ii;
        char uid[16];
        g_count = 0;
        for (ii = 0; ii < OQ_MAX_ACTIONS; ii++) {
            snprintf(uid, sizeof(uid), "u%d", ii);
            ASSERT_EQ_INT(OQ_ADD_ADDED, add(make_action(OQ_KIND_DELETE, uid, "", 0)));
        }
        ASSERT_EQ_INT(OQ_ADD_FULL, add(make_action(OQ_KIND_DELETE, "extra", "", 0)));
        g_count = 0;
    }
    TEST_CASE_END();
}

static void test_replay_policy(void)
{
    TEST_CASE("Rejeu: classement des erreurs (indication puis texte)");
    {
        ASSERT_EQ_INT(OQ_ERR_TRANSIENT, oq_classify_error(OQ_HINT_TRANSIENT, "peu importe"));
        ASSERT_EQ_INT(OQ_ERR_GONE, oq_classify_error(OQ_HINT_GONE, NULL));
        ASSERT_EQ_INT(OQ_ERR_PERMANENT, oq_classify_error(OQ_HINT_PERMANENT, "timed out"));
        ASSERT_EQ_INT(OQ_ERR_TRANSIENT, oq_classify_error(OQ_HINT_NONE, "Connection TIMED OUT"));
        ASSERT_EQ_INT(OQ_ERR_TRANSIENT, oq_classify_error(OQ_HINT_NONE, "Not available in offline mode"));
        ASSERT_EQ_INT(OQ_ERR_TRANSIENT, oq_classify_error(OQ_HINT_NONE, "Le serveur est hors connexion"));
        ASSERT_EQ_INT(OQ_ERR_GONE, oq_classify_error(OQ_HINT_NONE, "Message not found in folder"));
        ASSERT_EQ_INT(OQ_ERR_GONE, oq_classify_error(OQ_HINT_NONE, "Le message n'existe plus"));
        ASSERT_EQ_INT(OQ_ERR_PERMANENT, oq_classify_error(OQ_HINT_NONE, "Permission denied"));
        ASSERT_EQ_INT(OQ_ERR_PERMANENT, oq_classify_error(OQ_HINT_NONE, NULL));
        ASSERT_EQ_INT(OQ_ERR_PERMANENT, oq_classify_error(OQ_HINT_NONE, ""));
    }
    TEST_CASE_END();

    TEST_CASE("Rejeu: delai exponentiel plafonne");
    {
        ASSERT_TRUE(oq_retry_delay(1) == 30);
        ASSERT_TRUE(oq_retry_delay(2) == 60);
        ASSERT_TRUE(oq_retry_delay(3) == 120);
        ASSERT_TRUE(oq_retry_delay(4) == 240);
        ASSERT_TRUE(oq_retry_delay(20) == OQ_RETRY_MAX_SECONDS);
    }
    TEST_CASE_END();

    TEST_CASE("Rejeu: erreur transitoire = action conservee prete, erreur definitive = attente puis abandon");
    {
        OfflineAction a = make_action(OQ_KIND_DELETE, "1", "", 0);
        int ii;

        oq_record_failure(&a, OQ_ERR_TRANSIENT, "reseau coupe", 1000);
        ASSERT_EQ_INT(0, a.attempts);
        ASSERT_EQ_INT(OQ_REPLAY_READY, oq_replay_decision(&a, 1000, 0));
        ASSERT_EQ_STR("reseau coupe", a.error);

        oq_record_failure(&a, OQ_ERR_PERMANENT, "refuse", 1000);
        ASSERT_EQ_INT(1, a.attempts);
        ASSERT_TRUE(a.next_try == 1030);
        ASSERT_EQ_INT(OQ_REPLAY_WAIT, oq_replay_decision(&a, 1010, 0));
        ASSERT_EQ_INT(OQ_REPLAY_READY, oq_replay_decision(&a, 1030, 0));
        ASSERT_EQ_INT(OQ_REPLAY_READY, oq_replay_decision(&a, 1010, 1));

        for (ii = a.attempts; ii < OQ_MAX_ATTEMPTS; ii++)
            oq_record_failure(&a, OQ_ERR_PERMANENT, "refuse", 2000);
        ASSERT_EQ_INT(OQ_STATUS_FAILED, a.status);
        ASSERT_EQ_INT(OQ_REPLAY_SKIP, oq_replay_decision(&a, 99999, 0));
        ASSERT_EQ_INT(OQ_REPLAY_READY, oq_replay_decision(&a, 99999, 1));
        ASSERT_EQ_INT(0, oq_count_pending(&a, 1));

        oq_action_reset(&a);
        ASSERT_EQ_INT(OQ_STATUS_PENDING, a.status);
        ASSERT_EQ_INT(0, a.attempts);
        ASSERT_EQ_STR("", a.error);
        ASSERT_EQ_INT(1, oq_count_pending(&a, 1));
    }
    TEST_CASE_END();
}

static void test_conflicts(void)
{
    char rev_a[CF_REVISION_SIZE], rev_b[CF_REVISION_SIZE], part[64];

    TEST_CASE("Conflits: jeton de revision et extraction des composants");
    {
        cf_revision_make(rev_a, sizeof(rev_a), "\"abc\"", "20261111T080000Z", 3, "BEGIN:VEVENT");
        ASSERT_CONTAINS(rev_a, "e=\"abc\";m=20261111T080000Z;s=3;h=");
        ASSERT_TRUE(cf_revision_part(rev_a, 'e', part, sizeof(part)));
        ASSERT_EQ_STR("\"abc\"", part);
        ASSERT_TRUE(cf_revision_part(rev_a, 's', part, sizeof(part)));
        ASSERT_EQ_STR("3", part);
        cf_revision_make(rev_b, sizeof(rev_b), NULL, NULL, -1, "x");
        ASSERT_FALSE(cf_revision_part(rev_b, 'e', part, sizeof(part)));
        ASSERT_FALSE(cf_revision_part(rev_b, 's', part, sizeof(part)));
        ASSERT_TRUE(cf_revision_part(rev_b, 'h', part, sizeof(part)));
        ASSERT_TRUE(cf_hash_string("a") != cf_hash_string("b"));
        ASSERT_TRUE(cf_hash_string(NULL) == cf_hash_string(""));
    }
    TEST_CASE_END();

    TEST_CASE("Conflits: detection (ETag prioritaire, sinon date / sequence / empreinte)");
    {
        char x[CF_REVISION_SIZE], y[CF_REVISION_SIZE];

        ASSERT_EQ_INT(CF_NO_CONFLICT, cf_detect(NULL, "e=1;m=;s=;h=0"));
        ASSERT_EQ_INT(CF_NO_CONFLICT, cf_detect("", "e=1"));
        ASSERT_EQ_INT(CF_NO_CONFLICT, cf_detect("e=1;m=;s=;h=0", NULL));

        cf_revision_make(x, sizeof(x), "v1", "t1", 0, "contenu");
        cf_revision_make(y, sizeof(y), "v1", "t1", 0, "contenu");
        ASSERT_EQ_INT(CF_NO_CONFLICT, cf_detect(x, y));

        /* ETag identique : le contenu n'est pas examine */
        cf_revision_make(y, sizeof(y), "v1", "t2", 1, "autre");
        ASSERT_EQ_INT(CF_NO_CONFLICT, cf_detect(x, y));
        cf_revision_make(y, sizeof(y), "v2", "t1", 0, "contenu");
        ASSERT_EQ_INT(CF_REMOTE_CHANGED, cf_detect(x, y));

        /* sans ETag : date, sequence ou empreinte */
        cf_revision_make(x, sizeof(x), NULL, "t1", 0, "contenu");
        cf_revision_make(y, sizeof(y), NULL, "t1", 0, "contenu");
        ASSERT_EQ_INT(CF_NO_CONFLICT, cf_detect(x, y));
        cf_revision_make(y, sizeof(y), NULL, "t2", 0, "contenu");
        ASSERT_EQ_INT(CF_REMOTE_CHANGED, cf_detect(x, y));
        cf_revision_make(y, sizeof(y), NULL, "t1", 1, "contenu");
        ASSERT_EQ_INT(CF_REMOTE_CHANGED, cf_detect(x, y));
        cf_revision_make(y, sizeof(y), NULL, "t1", 0, "modifie");
        ASSERT_EQ_INT(CF_REMOTE_CHANGED, cf_detect(x, y));
    }
    TEST_CASE_END();

    TEST_CASE("Conflits: la resolution explicite leve le blocage");
    {
        ASSERT_EQ_INT(CF_RESOLVE_LOCAL, cf_resolution_from_name("local"));
        ASSERT_EQ_INT(CF_RESOLVE_REMOTE, cf_resolution_from_name("remote"));
        ASSERT_EQ_INT(CF_RESOLVE_BOTH, cf_resolution_from_name("both"));
        ASSERT_EQ_INT(CF_RESOLVE_NONE, cf_resolution_from_name(""));
        ASSERT_EQ_INT(CF_RESOLVE_NONE, cf_resolution_from_name(NULL));
        ASSERT_TRUE(cf_must_block("e=1", "e=2", CF_RESOLVE_NONE));
        ASSERT_FALSE(cf_must_block("e=1", "e=2", CF_RESOLVE_LOCAL));
        ASSERT_FALSE(cf_must_block("e=1", "e=2", CF_RESOLVE_BOTH));
        ASSERT_FALSE(cf_must_block("e=1", "e=1", CF_RESOLVE_NONE));
        ASSERT_FALSE(cf_must_block("", "e=2", CF_RESOLVE_NONE));
    }
    TEST_CASE_END();

    TEST_CASE("Conflits: champs en desaccord entre version locale et distante");
    {
        CfField local[3], remote[2];
        int diff[8], n;

        memset(local, 0, sizeof(local));
        memset(remote, 0, sizeof(remote));
        snprintf(local[0].name, sizeof(local[0].name), "summary");
        snprintf(local[0].value, sizeof(local[0].value), "Reunion");
        snprintf(local[1].name, sizeof(local[1].name), "location");
        snprintf(local[1].value, sizeof(local[1].value), "Salle A");
        snprintf(local[2].name, sizeof(local[2].name), "notes");
        snprintf(local[2].value, sizeof(local[2].value), "");
        snprintf(remote[0].name, sizeof(remote[0].name), "summary");
        snprintf(remote[0].value, sizeof(remote[0].value), "Reunion");
        snprintf(remote[1].name, sizeof(remote[1].name), "location");
        snprintf(remote[1].value, sizeof(remote[1].value), "Salle B");

        n = cf_diff_fields(local, 3, remote, 2, diff, 8);
        ASSERT_EQ_INT(1, n);
        ASSERT_EQ_STR("location", local[diff[0]].name);
        /* champ absent cote distant = vide : "notes" local vide n'est pas un desaccord */
        ASSERT_EQ_STR("Salle B", cf_field_value(remote, 2, "location"));
        ASSERT_NULL(cf_field_value(remote, 2, "notes"));
        /* capacite limitee */
        n = cf_diff_fields(local, 3, remote, 0, diff, 1);
        ASSERT_EQ_INT(1, n);
    }
    TEST_CASE_END();
}

/**
 * @brief Point d'entree de la suite de tests de la file hors ligne et des conflits.
 * @return 0 si succes, 1 en cas d'echec.
 */
int main(void)
{
    TEST_SUITE_BEGIN("File d'attente hors ligne & Conflits");

    test_queue_basics();
    test_queue_coalescing();
    test_replay_policy();
    test_conflicts();

    return test_suite_summary();
}
