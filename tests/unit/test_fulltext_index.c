/**
 * @file test_fulltext_index.c
 * @brief Tests unitaires de l'index plein texte local (winui/core/evo-core-index-util.h) :
 * tokenisation (casse, accents, ligatures, idéogrammes), index inversé (ajout, remplacement,
 * suppression, compaction), requêtes (ET, préfixes), sérialisation et résistance aux données
 * corrompues, détection des pièces jointes indexables.
 */

#include "../include/test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../winui/core/evo-core-index-util.h"

/** Accumule les mots produits par la tokenisation, séparés par « | ». */
static int collect_token(const char *token, size_t length, void *user_data)
{
    char *out = (char *) user_data;
    size_t used = strlen(out);

    if (used > 0)
        out[used++] = '|';
    memcpy(out + used, token, length);
    out[used + length] = '\0';
    return 0;
}

/** Tokenise un texte et retourne les mots joints par « | » dans un tampon statique. */
static const char *tokens_of(const char *text)
{
    static char out[1024];

    out[0] = '\0';
    evoidx_tokenize(text, text ? strlen(text) : 0, EVOIDX_MIN_TOKEN_CHARS, collect_token, out);
    return out;
}

/** Cherche dans un index et retourne le nombre de résultats (-1 si la requête n'est pas évaluable). */
static int count_hits(EvoIdx *idx, const char *query)
{
    const char **keys = NULL;
    size_t count = 0;
    int ok = evoidx_query(idx, query, &keys, &count);

    free((void *) keys);
    return ok ? (int) count : -1;
}

/** Les résultats d'une requête contiennent-ils cette clé ? */
static int hits_contain(EvoIdx *idx, const char *query, const char *key)
{
    const char **keys = NULL;
    size_t count = 0, ii;
    int found = 0;

    if (evoidx_query(idx, query, &keys, &count)) {
        for (ii = 0; ii < count; ii++) {
            if (strcmp(keys[ii], key) == 0)
                found = 1;
        }
    }
    free((void *) keys);
    return found;
}

/** Remplit un index de trois messages d'exemple. */
static EvoIdx *sample_index(void)
{
    EvoIdx *idx = evoidx_new();
    const char *t1 = "Bonjour, la réunion du budget est déplacée à jeudi.";
    const char *t2 = "Facture n°4521 : paiement de l'École Supérieure, merci.";
    const char *t3 = "Meeting about the budget and the roadmap.";

    evoidx_add_document(idx, "1", t1, strlen(t1));
    evoidx_add_document(idx, "2", t2, strlen(t2));
    evoidx_add_document(idx, "3", t3, strlen(t3));
    return idx;
}

static void test_tokenizer(void)
{
    TEST_CASE("Index: casse et accents repliés, ponctuation séparatrice");
    {
        ASSERT_EQ_STR("bonjour|la|reunion|du|budget|est|deplacee|jeudi", tokens_of("Bonjour, la réunion du budget est déplacée à jeudi"));
        ASSERT_EQ_STR("ecole|superieure", tokens_of("ÉCOLE Supérieure"));
        ASSERT_EQ_STR("pierre|christensen|free|fr", tokens_of("pierre.christensen@free.fr"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: ligatures, chiffres, mots trop courts ou trop longs");
    {
        ASSERT_EQ_STR("coeur|strasse|aeroport", tokens_of("Cœur Straße Æroport"));
        ASSERT_EQ_STR("4521|ab", tokens_of("n°4521 a ab"));
        ASSERT_EQ_STR("ok", tokens_of("ok x y"));
        ASSERT_EQ_STR("", tokens_of("a b c"));
        ASSERT_EQ_STR("", tokens_of(NULL));
        ASSERT_EQ_STR("court", tokens_of("court aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: cyrillique, grec, idéogrammes, UTF-8 invalide");
    {
        ASSERT_EQ_STR("привет|мир", tokens_of("ПРИВЕТ Мир"));
        ASSERT_EQ_STR("αθηνα", tokens_of("Αθήνα"));
        ASSERT_EQ_STR("東|京|ab", tokens_of("東京 ab"));
        ASSERT_EQ_STR("abc|def", tokens_of("abc\xFF" "def"));
        ASSERT_EQ_STR("abc", tokens_of("abc\xC3"));
    }
    TEST_CASE_END();
}

static void test_query_and_prefix(void)
{
    EvoIdx *idx = sample_index();

    TEST_CASE("Index: recherche d'un mot, insensible à la casse et aux accents");
    {
        ASSERT_EQ_INT(2, count_hits(idx, "budget"));
        ASSERT_EQ_INT(1, count_hits(idx, "ECOLE"));
        ASSERT_EQ_INT(1, count_hits(idx, "école"));
        ASSERT_EQ_INT(1, count_hits(idx, "deplacee"));
        ASSERT_EQ_INT(0, count_hits(idx, "inexistant"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: tous les mots sont requis (ET)");
    {
        ASSERT_EQ_INT(1, count_hits(idx, "budget jeudi"));
        ASSERT_EQ_INT(1, count_hits(idx, "budget roadmap"));
        ASSERT_EQ_INT(0, count_hits(idx, "jeudi roadmap"));
        ASSERT_TRUE(hits_contain(idx, "budget meeting", "3"));
        ASSERT_FALSE(hits_contain(idx, "budget meeting", "1"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: préfixes (recherche au fil de la saisie)");
    {
        ASSERT_EQ_INT(2, count_hits(idx, "budg"));
        ASSERT_EQ_INT(1, count_hits(idx, "fact"));
        ASSERT_EQ_INT(1, count_hits(idx, "sup"));
        ASSERT_EQ_INT(0, count_hits(idx, "gets"));   /* pas de sous-chaîne au milieu d'un mot */
        ASSERT_EQ_INT(2, count_hits(idx, "bu"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: requête sans mot utilisable, non évaluable");
    {
        ASSERT_EQ_INT(-1, count_hits(idx, ""));
        ASSERT_EQ_INT(-1, count_hits(idx, "a"));
        ASSERT_EQ_INT(-1, count_hits(idx, " , ; "));
        ASSERT_EQ_INT(-1, count_hits(NULL, "budget"));
    }
    TEST_CASE_END();

    evoidx_free(idx);
}

static void test_update_remove_compact(void)
{
    EvoIdx *idx = sample_index();
    const char *newer = "Le budget est annulé";
    char key[32], text[64];
    int ii;

    TEST_CASE("Index: remplacement d'un document de même clé");
    {
        ASSERT_EQ_INT(3, (int) evoidx_document_count(idx));
        evoidx_add_document(idx, "1", newer, strlen(newer));
        ASSERT_EQ_INT(3, (int) evoidx_document_count(idx));
        ASSERT_EQ_INT(0, count_hits(idx, "jeudi"));
        ASSERT_EQ_INT(1, count_hits(idx, "annule"));
        ASSERT_EQ_INT(2, count_hits(idx, "budget"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: suppression d'un document");
    {
        ASSERT_TRUE(evoidx_has_document(idx, "3"));
        ASSERT_TRUE(evoidx_remove_document(idx, "3"));
        ASSERT_FALSE(evoidx_has_document(idx, "3"));
        ASSERT_FALSE(evoidx_remove_document(idx, "3"));
        ASSERT_EQ_INT(0, count_hits(idx, "roadmap"));
        ASSERT_EQ_INT(1, count_hits(idx, "budget"));
    }
    TEST_CASE_END();

    TEST_CASE("Index: compaction automatique après de nombreuses suppressions");
    {
        for (ii = 0; ii < 300; ii++) {
            snprintf(key, sizeof(key), "m%d", ii);
            snprintf(text, sizeof(text), "message numero%d sujet commun", ii);
            evoidx_add_document(idx, key, text, strlen(text));
        }
        ASSERT_EQ_INT(300, count_hits(idx, "commun"));
        for (ii = 0; ii < 290; ii++) {
            snprintf(key, sizeof(key), "m%d", ii);
            evoidx_remove_document(idx, key);
        }
        ASSERT_EQ_INT(10, count_hits(idx, "commun"));
        ASSERT_EQ_INT(0, count_hits(idx, "numero5"));
        ASSERT_EQ_INT(1, count_hits(idx, "numero295"));
        ASSERT_TRUE(hits_contain(idx, "numero299", "m299"));
        ASSERT_EQ_INT(12, (int) evoidx_document_count(idx));
        ASSERT_TRUE(idx->n_docs < 100);   /* la compaction a eu lieu */
    }
    TEST_CASE_END();

    evoidx_free(idx);
}

/** Compte les clés visitées par evoidx_foreach_document. */
static int count_key(const char *key, void *user_data)
{
    (void) key;
    (*(int *) user_data)++;
    return 0;
}

static void test_serialization(void)
{
    EvoIdx *idx = sample_index(), *copy;
    unsigned char *data;
    size_t length = 0, cut;
    int visited = 0;

    TEST_CASE("Index: sérialisation puis rechargement identique");
    {
        evoidx_remove_document(idx, "2");
        data = evoidx_serialize(idx, &length);
        ASSERT_NOT_NULL(data);
        copy = evoidx_deserialize(data, length);
        ASSERT_NOT_NULL(copy);
        if (copy) {
            ASSERT_EQ_INT(2, (int) evoidx_document_count(copy));
            ASSERT_EQ_INT(2, count_hits(copy, "budget"));
            ASSERT_EQ_INT(0, count_hits(copy, "facture"));
            ASSERT_TRUE(hits_contain(copy, "roadmap", "3"));
            evoidx_foreach_document(copy, count_key, &visited);
            ASSERT_EQ_INT(2, visited);
            evoidx_add_document(copy, "9", "nouveau budget", 14);
            ASSERT_EQ_INT(3, count_hits(copy, "budget"));
            evoidx_free(copy);
        }
    }
    TEST_CASE_END();

    TEST_CASE("Index: fichiers tronqués ou corrompus refusés sans plantage");
    {
        ASSERT_NULL(evoidx_deserialize(NULL, 0));
        ASSERT_NULL(evoidx_deserialize((const unsigned char *) "EVIX", 4));
        ASSERT_NULL(evoidx_deserialize((const unsigned char *) "nimportequoi-nimportequoi", 25));
        for (cut = 0; data && cut < length; cut += 7) {
            copy = evoidx_deserialize(data, cut);
            ASSERT_NULL(copy);
            evoidx_free(copy);
        }
        if (data) {
            unsigned char *bad = (unsigned char *) malloc(length);
            size_t ii;

            memcpy(bad, data, length);
            for (ii = 12; ii < length; ii += 5)
                bad[ii] ^= 0xFF;   /* corruption : ne doit jamais planter */
            copy = evoidx_deserialize(bad, length);
            evoidx_free(copy);
            free(bad);
        }
    }
    TEST_CASE_END();

    free(data);
    evoidx_free(idx);
}

static void test_attachments(void)
{
    TEST_CASE("Index: pièces jointes indexables (texte) ou non (binaires)");
    {
        ASSERT_TRUE(evoidx_is_indexable_attachment("text/plain", NULL));
        ASSERT_TRUE(evoidx_is_indexable_attachment("TEXT/CSV; charset=utf-8", "x.bin"));
        ASSERT_TRUE(evoidx_is_indexable_attachment("application/json", NULL));
        ASSERT_TRUE(evoidx_is_indexable_attachment("application/octet-stream", "Notes.TXT"));
        ASSERT_TRUE(evoidx_is_indexable_attachment(NULL, "export.csv"));
        ASSERT_FALSE(evoidx_is_indexable_attachment("application/pdf", "contrat.pdf"));
        ASSERT_FALSE(evoidx_is_indexable_attachment("image/png", "photo.png"));
        ASSERT_FALSE(evoidx_is_indexable_attachment("application/octet-stream", "archive.zip"));
        ASSERT_FALSE(evoidx_is_indexable_attachment(NULL, NULL));
        ASSERT_FALSE(evoidx_is_indexable_attachment("", "txt"));
    }
    TEST_CASE_END();
}

/**
 * @brief Point d'entrée de la suite de tests de l'index plein texte.
 * @return 0 si succès, 1 en cas d'échec.
 */
int main(void)
{
    TEST_SUITE_BEGIN("Index plein texte local (evo-core-index-util.h)");

    test_tokenizer();
    test_query_and_prefix();
    test_update_remove_compact();
    test_serialization();
    test_attachments();

    return test_suite_summary();
}
