/**
 * @file test_transfer.c
 * @brief Tests unitaires des fonctions pures d'export / import du courrier et des contacts
 * (evo-core-transfer-util.h) : format mbox (protection des lignes « From »), nommage sûr
 * des fichiers, plage de dates, indicateurs Thunderbird / Maildir, découpage vCard.
 */

#include "../include/test_framework.h"
#include "../../winui/core/evo-core-transfer-util.h"

/** Protection puis restauration des lignes « From » : aller-retour exact. */
static void test_mbox_quote(void)
{
    TEST_CASE("mbox: protection des lignes From (mboxrd) et aller-retour");
    {
        const char *msg = "Subject: a\n\nFrom here we go\n>From already\n>>From deeper\nFromage\nfin";
        size_t qlen = 0, ulen = 0;
        char *q = mt_mbox_quote(msg, strlen(msg), &qlen);
        char *u;

        ASSERT_NOT_NULL(q);
        ASSERT_TRUE(strstr(q, "\n>From here we go\n") != NULL);
        ASSERT_TRUE(strstr(q, "\n>>From already\n") != NULL);
        ASSERT_TRUE(strstr(q, "\n>>>From deeper\n") != NULL);
        ASSERT_TRUE(strstr(q, "\nFromage\n") != NULL);
        /* terminé par une ligne vide */
        ASSERT_TRUE(qlen >= 2 && q[qlen - 1] == '\n' && q[qlen - 2] == '\n');

        u = mt_mbox_unquote(q, qlen, &ulen);
        ASSERT_NOT_NULL(u);
        /* le message d'origine suivi des deux fins de ligne ajoutées */
        ASSERT_TRUE(strncmp(u, msg, strlen(msg)) == 0);
        ASSERT_EQ_INT((int)(strlen(msg) + 2), (int)ulen);
        free(u);
        free(q);
    }
    TEST_CASE_END();
}

/** Découpage d'un mbox de plusieurs messages. */
static void test_mbox_split(void)
{
    TEST_CASE("mbox: separation des messages et retrait de la ligne vide finale");
    {
        const char *file =
            "From a@x.org Mon Jan  1 10:00:00 2024\nSubject: un\n\ncorps un\n\n"
            "From b@x.org Tue Jan  2 11:30:00 2024\nSubject: deux\n\n>From ligne protegee\n\n"
            "From c@x.org Wed Jan  3 12:00:00 2024\nSubject: trois\n\ndernier\n";
        MtMboxReader r;
        const char *start;
        size_t len;
        char *u;
        size_t ulen;

        mt_mbox_reader_init(&r, file, strlen(file));
        ASSERT_TRUE(mt_mbox_next(&r, &start, &len));
        ASSERT_EQ_INT((int)strlen("Subject: un\n\ncorps un\n"), (int)len);
        ASSERT_TRUE(strncmp(start, "Subject: un\n\ncorps un\n", len) == 0);

        ASSERT_TRUE(mt_mbox_next(&r, &start, &len));
        u = mt_mbox_unquote(start, len, &ulen);
        ASSERT_TRUE(strstr(u, "\nFrom ligne protegee\n") != NULL);
        free(u);

        ASSERT_TRUE(mt_mbox_next(&r, &start, &len));
        ASSERT_TRUE(strncmp(start, "Subject: trois\n\ndernier\n", len) == 0);
        ASSERT_FALSE(mt_mbox_next(&r, &start, &len));
    }
    TEST_CASE_END();
}

/** Séparateurs reconnus ou refusés, fins de ligne CRLF, fichier non mbox. */
static void test_mbox_detect(void)
{
    TEST_CASE("mbox: detection des separateurs et des fichiers mbox");
    {
        ASSERT_TRUE(mt_is_from_line("From a@b.c Mon Jan  1 10:00:00 2024", 35));
        ASSERT_FALSE(mt_is_from_line("From here we go", 15));
        ASSERT_FALSE(mt_is_from_line("From  x", 7));
        ASSERT_FALSE(mt_is_from_line("Subject: x", 10));
        ASSERT_TRUE(mt_looks_like_mbox("From a@b.c Mon Jan  1 10:00:00 2024\r\nX: y\r\n", 40));
        ASSERT_FALSE(mt_looks_like_mbox("Received: from x\n", 17));
        {
            const char *file = "From a@b.c Mon Jan  1 10:00:00 2024\r\nSubject: crlf\r\n\r\ncorps\r\n\r\n"
                               "From d@b.c Mon Jan  1 10:00:00 2024\r\nSubject: deux\r\n\r\nx\r\n";
            MtMboxReader r;
            const char *start;
            size_t len;

            mt_mbox_reader_init(&r, file, strlen(file));
            ASSERT_TRUE(mt_mbox_next(&r, &start, &len));
            ASSERT_EQ_INT((int)strlen("Subject: crlf\r\n\r\ncorps\r\n"), (int)len);
            ASSERT_TRUE(mt_mbox_next(&r, &start, &len));
            ASSERT_FALSE(mt_mbox_next(&r, &start, &len));
        }
    }
    TEST_CASE_END();
}

/** Ligne « From » : adresse nettoyée et date UTC. */
static void test_from_line(void)
{
    TEST_CASE("mbox: ligne From (adresse, date UTC, jour de la semaine)");
    {
        char line[160];

        /* 2024-03-05 14:07:09 UTC = mardi */
        mt_from_line("jean dupont@x.org", 1709647629, line, sizeof(line));
        ASSERT_EQ_STR("From jean_dupont@x.org Tue Mar  5 14:07:09 2024\n", line);
        mt_from_line("", 0, line, sizeof(line));
        ASSERT_EQ_STR("From MAILER-DAEMON Thu Jan  1 00:00:00 1970\n", line);
        /* 29 février d'une année bissextile */
        mt_from_line("a@b", 1709164800, line, sizeof(line));
        ASSERT_EQ_STR("From a@b Thu Feb 29 00:00:00 2024\n", line);
    }
    TEST_CASE_END();
}

/** Noms de fichiers Windows sûrs. */
static void test_safe_name(void)
{
    TEST_CASE("nommage sur: caracteres interdits, noms reserves, longueur, UTF-8");
    {
        char out[200], longname[400];
        int ii;

        mt_safe_name("Re: a/b\\c?*\"<>|", "Message", out, sizeof(out));
        ASSERT_EQ_STR("Re_ a_b_c______", out);
        mt_safe_name("  fin...  ", "Message", out, sizeof(out));
        ASSERT_EQ_STR("fin", out);
        mt_safe_name("", "Message", out, sizeof(out));
        ASSERT_EQ_STR("Message", out);
        mt_safe_name(NULL, "Dossier", out, sizeof(out));
        ASSERT_EQ_STR("Dossier", out);
        mt_safe_name("...", "Message", out, sizeof(out));
        ASSERT_EQ_STR("Message", out);
        mt_safe_name("con", "x", out, sizeof(out));
        ASSERT_EQ_STR("_con", out);
        mt_safe_name("COM3.txt", "x", out, sizeof(out));
        ASSERT_EQ_STR("_COM3.txt", out);
        mt_safe_name("console", "x", out, sizeof(out));
        ASSERT_EQ_STR("console", out);
        /* 'é' (deux octets) répété : la coupe ne doit jamais tomber au milieu */
        for (ii = 0; ii < 398; ii += 2) {
            longname[ii] = '\xC3';
            longname[ii + 1] = '\xA9';
        }
        longname[398] = '\0';
        mt_safe_name(longname, "x", out, sizeof(out));
        ASSERT_TRUE(strlen(out) <= MT_MAX_NAME_BYTES);
        ASSERT_EQ_INT(0, (int)(strlen(out) % 2));
    }
    TEST_CASE_END();
}

/** Plage de dates. */
static void test_range(void)
{
    TEST_CASE("plage de dates: bornes facultatives et incluses");
    {
        ASSERT_TRUE(mt_in_range(100, 0, 0));
        ASSERT_TRUE(mt_in_range(100, 100, 200));
        ASSERT_TRUE(mt_in_range(200, 100, 200));
        ASSERT_FALSE(mt_in_range(99, 100, 200));
        ASSERT_FALSE(mt_in_range(201, 100, 200));
        ASSERT_TRUE(mt_in_range(5, 0, 10));
        ASSERT_FALSE(mt_in_range(11, 0, 10));
    }
    TEST_CASE_END();
}

/** Indicateurs Thunderbird et Maildir. */
static void test_flags(void)
{
    TEST_CASE("indicateurs: X-Mozilla-Status et suffixe Maildir");
    {
        const char *read = "X-Mozilla-Status: 0001\nSubject: x\n\ncorps";
        const char *flagged = "Subject: x\nX-Mozilla-Status: 0005\n\ncorps";
        const char *deleted = "X-Mozilla-Status: 0009\n\ncorps";
        const char *none = "Subject: x\n\nX-Mozilla-Status: 0000\n";
        const char *unread = "X-Mozilla-Status: 0000\n\n";
        char value[40];

        ASSERT_EQ_INT(MT_FLAG_SEEN, mt_mozilla_flags(read, strlen(read)));
        ASSERT_EQ_INT(MT_FLAG_SEEN | MT_FLAG_FLAGGED, mt_mozilla_flags(flagged, strlen(flagged)));
        ASSERT_TRUE(mt_mozilla_flags(deleted, strlen(deleted)) & MT_FLAG_DELETED);
        /* en-tête absent des en-têtes (dans le corps) : courrier d'archive, lu */
        ASSERT_EQ_INT(MT_FLAG_SEEN, mt_mozilla_flags(none, strlen(none)));
        ASSERT_EQ_INT(0, mt_mozilla_flags(unread, strlen(unread)));
        ASSERT_TRUE(mt_header_value(flagged, strlen(flagged), "subject", value, sizeof(value)));
        ASSERT_EQ_STR("x", value);

        ASSERT_EQ_INT(MT_FLAG_SEEN, mt_maildir_flags("1700000000.M1P2.host:2,S", 0));
        ASSERT_EQ_INT(MT_FLAG_SEEN | MT_FLAG_REPLIED | MT_FLAG_FLAGGED, mt_maildir_flags("1.x:2,FRS", 0));
        ASSERT_TRUE(mt_maildir_flags("1.x:2,ST", 0) & MT_FLAG_DELETED);
        ASSERT_EQ_INT(MT_FLAG_SEEN | MT_FLAG_FLAGGED, mt_maildir_flags("1.x!2,SF", 0));
        ASSERT_EQ_INT(MT_FLAG_SEEN, mt_maildir_flags("1.x;2,S", 1));
        ASSERT_EQ_INT(0, mt_maildir_flags("1.x", 1));
        ASSERT_EQ_INT(MT_FLAG_SEEN, mt_maildir_flags("1.x", 0));
    }
    TEST_CASE_END();
}

/** Noms et fichiers d'un profil Thunderbird, Maildir++. */
static void test_thunderbird(void)
{
    TEST_CASE("Thunderbird / Maildir: fichiers auxiliaires, noms de dossiers");
    {
        char out[100];

        ASSERT_TRUE(mt_is_aux_file("INBOX.msf"));
        ASSERT_TRUE(mt_is_aux_file("filterlog.html"));
        ASSERT_TRUE(mt_is_aux_file("msgFilterRules.dat"));
        ASSERT_TRUE(mt_is_aux_file("Archives.sbd"));
        ASSERT_TRUE(mt_is_aux_file(".hidden"));
        ASSERT_FALSE(mt_is_aux_file("INBOX"));
        ASSERT_FALSE(mt_is_aux_file("Sent"));
        ASSERT_FALSE(mt_is_aux_file("Courrier.mbox"));

        mt_tb_folder_name("Archives.sbd", out, sizeof(out));
        ASSERT_EQ_STR("Archives", out);
        mt_tb_folder_name("Sent", out, sizeof(out));
        ASSERT_EQ_STR("Sent", out);
        mt_maildirpp_path(".Travail.Projets", out, sizeof(out));
        ASSERT_EQ_STR("Travail/Projets", out);
        mt_strip_ext("Courrier.mbox", out, sizeof(out));
        ASSERT_EQ_STR("Courrier", out);
        mt_strip_ext(".mbox", out, sizeof(out));
        ASSERT_EQ_STR(".mbox", out);
        ASSERT_TRUE(mt_has_ext("A.EML", ".eml"));
        ASSERT_FALSE(mt_has_ext(".eml", ".eml"));
    }
    TEST_CASE_END();
}

/** Découpage d'un fichier .vcf. */
static void test_vcard(void)
{
    TEST_CASE("vCard: decoupage d'un fichier de plusieurs cartes");
    {
        const char *file = "junk\r\nBEGIN:VCARD\r\nVERSION:3.0\r\nFN:A\r\nEND:VCARD\r\n\r\n"
                           "begin:vcard\nFN:B\nEND:VCARD";
        size_t pos = 0, len;
        const char *start;

        ASSERT_TRUE(mt_vcard_next(file, strlen(file), &pos, &start, &len));
        ASSERT_TRUE(strncmp(start, "BEGIN:VCARD", 11) == 0);
        ASSERT_TRUE(strstr(start, "FN:A") != NULL && len < 60);
        ASSERT_TRUE(mt_vcard_next(file, strlen(file), &pos, &start, &len));
        ASSERT_TRUE(strncmp(start, "begin:vcard", 11) == 0);
        ASSERT_FALSE(mt_vcard_next(file, strlen(file), &pos, &start, &len));
    }
    TEST_CASE_END();
}

/**
 * @brief Point d'entree de la suite de tests d'export / import.
 * @return 0 si succes, 1 en cas d'echec.
 */
int main(void)
{
    TEST_SUITE_BEGIN("Export / Import du courrier et des contacts");

    test_mbox_quote();
    test_mbox_split();
    test_mbox_detect();
    test_from_line();
    test_safe_name();
    test_range();
    test_flags();
    test_thunderbird();
    test_vcard();

    return test_suite_summary();
}
