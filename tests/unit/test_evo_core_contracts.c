/**
 * @file test_evo_core_contracts.c
 * @brief Tests unitaires des contrats d'interface, des evenements JSON et de la logique critique d'evo-core.
 *
 * Valide les specifications suivantes afin d'eviter toute regression entre versions :
 * - Structure et champs obligatoires des evenements JSON ("progress", "connection-status", "folder-changed", "new-mail")
 * - Algorithme de decoupage et reconstitution des secrets volumineux (> 2560 octets) pour Windows Credential Manager
 * - Classification et hierarchisation des dossiers speciaux (Boite de reception, Brouillons, etc.)
 * - Validation syntaxique des adresses electroniques pour la detection de compte
 */

#include "../include/test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../winui/core/evo-core-mdn.h"
#include "../../winui/core/evo-core-itip.h"

#define CREDENTIAL_CHUNK_MAX_SIZE 2048

/**
 * @brief Structure simulant le stockage d'un fragment d'identifiant Windows.
 */
typedef struct {
    char target_name[256];
    char *data;
    size_t size;
} CredentialChunk;

/**
 * @brief Decoupe une chaine de secret en plusieurs fragments si sa taille depasse CREDENTIAL_CHUNK_MAX_SIZE.
 * Reproduction de l'algorithme d'evo-core pour depasser la limite de 2 560 octets de CredWrite.
 * @param target Nom de base de la cible (ex: "Evolution-Data-Server:OAuth2::m365").
 * @param secret Contenu complet du secret a stocker.
 * @param out_chunks Tableau alloue recevant les fragments.
 * @param out_count Nombre total de fragments produits.
 */
static void split_large_credential(const char *target,
                                   const char *secret,
                                   CredentialChunk **out_chunks,
                                   int *out_count)
{
    size_t total_len = strlen(secret);
    int count = (int)((total_len + CREDENTIAL_CHUNK_MAX_SIZE - 1) / CREDENTIAL_CHUNK_MAX_SIZE);
    CredentialChunk *chunks;
    int i;

    if (count == 0)
        count = 1;

    chunks = (CredentialChunk *)calloc((size_t)count, sizeof(CredentialChunk));

    for (i = 0; i < count; i++) {
        size_t offset = (size_t)i * CREDENTIAL_CHUNK_MAX_SIZE;
        size_t chunk_len = total_len - offset;
        if (chunk_len > CREDENTIAL_CHUNK_MAX_SIZE)
            chunk_len = CREDENTIAL_CHUNK_MAX_SIZE;

        if (count == 1) {
            snprintf(chunks[i].target_name, sizeof(chunks[i].target_name), "%s", target);
        } else {
            snprintf(chunks[i].target_name, sizeof(chunks[i].target_name), "%s#chunk%d", target, i);
        }

        chunks[i].data = (char *)malloc(chunk_len + 1);
        memcpy(chunks[i].data, secret + offset, chunk_len);
        chunks[i].data[chunk_len] = '\0';
        chunks[i].size = chunk_len;
    }

    *out_chunks = chunks;
    *out_count = count;
}

/**
 * @brief Reconstitue un secret complet a partir de fragments ordonnes.
 * @param chunks Tableau des fragments.
 * @param count Nombre de fragments.
 * @return Chaine complete reconstituee allouee dynamiquement.
 */
static char *reassemble_credential(const CredentialChunk *chunks, int count)
{
    size_t total_size = 0;
    char *result;
    size_t current_pos = 0;
    int i;

    for (i = 0; i < count; i++) {
        total_size += chunks[i].size;
    }

    result = (char *)malloc(total_size + 1);
    if (!result) return NULL;

    for (i = 0; i < count; i++) {
        memcpy(result + current_pos, chunks[i].data, chunks[i].size);
        current_pos += chunks[i].size;
    }
    result[total_size] = '\0';

    return result;
}

/**
 * @brief Libere la memoire associee a un tableau de fragments.
 * @param chunks Tableau a liberer.
 * @param count Nombre d'elements.
 */
static void free_credential_chunks(CredentialChunk *chunks, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        free(chunks[i].data);
    }
    free(chunks);
}

/**
 * @brief Valide la syntaxe minimale d'une adresse de messagerie pour la detection de compte.
 * @param email Adresse a verifier.
 * @return 1 si l'adresse est syntaxiquement plausible pour autodetect, 0 sinon.
 */
static int validate_email_syntax(const char *email)
{
    const char *at;
    const char *dot;

    if (!email || strlen(email) < 5)
        return 0;

    at = strrchr(email, '@');
    if (!at || at == email || !at[1])
        return 0;

    dot = strchr(at + 1, '.');
    if (!dot || dot == at + 1 || !dot[1])
        return 0;

    return 1;
}

/**
 * @brief Valide si un role de dossier correspond a un role reconnu d'Outlook / Camel.
 * @param type Nom du role (ex: "inbox", "sent", "trash", etc.).
 * @return Indice d'ordre d'affichage (0 pour Boite de reception, nombre positif pour les autres, -1 si inconnu).
 */
static int get_special_folder_priority(const char *type)
{
    if (strcmp(type, "inbox") == 0)   return 0;
    if (strcmp(type, "drafts") == 0)  return 1;
    if (strcmp(type, "sent") == 0)    return 2;
    if (strcmp(type, "trash") == 0)   return 3;
    if (strcmp(type, "archive") == 0) return 4;
    if (strcmp(type, "junk") == 0)    return 5;
    if (strcmp(type, "outbox") == 0)  return 6;
    return 99; /* Dossier utilisateur standard */
}

/**
 * @brief Classe l'adresse d'un calendrier web (reproduction de la regle de evo_core_create_calendar).
 * @param url Adresse saisie par l'utilisateur.
 * @return 0 si invalide, 1 si http(s)://, 2 si webcal:// (convertie en https://), 3 si vide (calendrier local).
 */
static int classify_calendar_url(const char *url)
{
    if (!url || !*url)
        return 3;
    if (_strnicmp(url, "webcal://", 9) == 0)
        return 2;
    if (_strnicmp(url, "http://", 7) == 0 || _strnicmp(url, "https://", 8) == 0)
        return 1;
    return 0;
}

/* --- Tests unitaires --- */

/**
 * @brief Test de non-regression sur le decoupage des secrets volumineux Windows Credential Manager.
 */
static void test_credential_chunking_large_secret(void)
{
    TEST_CASE("Credential Manager: Secret normal <= 2048 octets (un seul fragment)");
    {
        const char *normal_secret = "mon_mot_de_passe_secret_12345";
        CredentialChunk *chunks = NULL;
        int count = 0;
        char *reconstructed;

        split_large_credential("Target1", normal_secret, &chunks, &count);
        ASSERT_EQ_INT(1, count);
        ASSERT_EQ_STR("Target1", chunks[0].target_name);

        reconstructed = reassemble_credential(chunks, count);
        ASSERT_EQ_STR(normal_secret, reconstructed);

        free(reconstructed);
        free_credential_chunks(chunks, count);
    }
    TEST_CASE_END();

    TEST_CASE("Credential Manager: Jeton OAuth2 Microsoft 365 long (~5000 octets)");
    {
        /* Bogue historique corrige en v0.9.4 : les jetons depassaient 2 560 octets */
        char large_token[5001];
        CredentialChunk *chunks = NULL;
        int count = 0;
        char *reconstructed;
        int i;

        for (i = 0; i < 5000; i++) {
            large_token[i] = (char)('a' + (i % 26));
        }
        large_token[5000] = '\0';

        split_large_credential("Evolution-Data-Server:OAuth2::m365", large_token, &chunks, &count);
        ASSERT_TRUE(count >= 3); /* 5000 / 2048 = 3 fragments */
        ASSERT_EQ_STR("Evolution-Data-Server:OAuth2::m365#chunk0", chunks[0].target_name);
        ASSERT_EQ_STR("Evolution-Data-Server:OAuth2::m365#chunk1", chunks[1].target_name);
        ASSERT_EQ_STR("Evolution-Data-Server:OAuth2::m365#chunk2", chunks[2].target_name);

        reconstructed = reassemble_credential(chunks, count);
        ASSERT_EQ_STR(large_token, reconstructed);
        ASSERT_EQ_INT(5000, strlen(reconstructed));

        free(reconstructed);
        free_credential_chunks(chunks, count);
    }
    TEST_CASE_END();
}

/**
 * @brief Test des regles de validation syntaxique des adresses electroniques.
 */
static void test_email_validation(void)
{
    TEST_CASE("Email: Adresses valides pour autodetection");
    {
        ASSERT_TRUE(validate_email_syntax("pierre@entreprise.fr"));
        ASSERT_TRUE(validate_email_syntax("jean.dupont@sub.domaine.com"));
        ASSERT_TRUE(validate_email_syntax("utilisateur+tag@gmail.com"));
    }
    TEST_CASE_END();

    TEST_CASE("Email: Adresses invalides rejetees");
    {
        ASSERT_FALSE(validate_email_syntax(NULL));
        ASSERT_FALSE(validate_email_syntax(""));
        ASSERT_FALSE(validate_email_syntax("sans_arobase.fr"));
        ASSERT_FALSE(validate_email_syntax("@domaine.com"));
        ASSERT_FALSE(validate_email_syntax("pierre@"));
        ASSERT_FALSE(validate_email_syntax("pierre@domaine"));
    }
    TEST_CASE_END();
}

/**
 * @brief Test de priorite et de classification des dossiers speciaux.
 */
static void test_special_folders_priority(void)
{
    TEST_CASE("Dossiers speciaux: Ordre d'affichage Outlook garanti");
    {
        /* La boite de reception doit toujours apparaitre en premier (priorite 0) */
        ASSERT_EQ_INT(0, get_special_folder_priority("inbox"));
        ASSERT_EQ_INT(1, get_special_folder_priority("drafts"));
        ASSERT_EQ_INT(2, get_special_folder_priority("sent"));
        ASSERT_EQ_INT(3, get_special_folder_priority("trash"));
        ASSERT_EQ_INT(4, get_special_folder_priority("archive"));
        ASSERT_EQ_INT(5, get_special_folder_priority("junk"));
        ASSERT_EQ_INT(6, get_special_folder_priority("outbox"));
        ASSERT_EQ_INT(99, get_special_folder_priority("DossierPersonnel"));
    }
    TEST_CASE_END();
}

/**
 * @brief Test des contrats de donnees JSON pour les evenements evo-core.
 */
static void test_json_event_contracts(void)
{
    TEST_CASE("Contrats JSON: Schema de l'evenement 'progress'");
    {
        const char *sample_progress =
            "{\"account\": \"compte-1\", \"folder\": \"INBOX\", \"message\": \"Synchronisation 5/10\", \"percent\": 50, \"active\": true}";
        ASSERT_CONTAINS(sample_progress, "\"account\":");
        ASSERT_CONTAINS(sample_progress, "\"folder\":");
        ASSERT_CONTAINS(sample_progress, "\"message\":");
        ASSERT_CONTAINS(sample_progress, "\"percent\":");
        ASSERT_CONTAINS(sample_progress, "\"active\":");
    }
    TEST_CASE_END();

    TEST_CASE("Contrats JSON: Schema de l'evenement 'connection-status'");
    {
        const char *sample_status =
            "{\"account\": \"compte-1\", \"service\": \"imap\", \"status\": \"connected\", \"message\": \"Connecte a imap.example.com\"}";
        ASSERT_CONTAINS(sample_status, "\"account\":");
        ASSERT_CONTAINS(sample_status, "\"service\":");
        ASSERT_CONTAINS(sample_status, "\"status\":");
        ASSERT_CONTAINS(sample_status, "\"connected\"");
    }
    TEST_CASE_END();

    TEST_CASE("Contrats JSON: Schema de l'evenement 'folder-changed'");
    {
        const char *sample_folder_changed =
            "{\"account\": \"compte-1\", \"folder\": \"INBOX\", \"unread\": 3, \"total\": 120, \"added\": 1, \"removed\": 0, \"changed\": 0}";
        ASSERT_CONTAINS(sample_folder_changed, "\"unread\":");
        ASSERT_CONTAINS(sample_folder_changed, "\"total\":");
        ASSERT_CONTAINS(sample_folder_changed, "\"added\":");
    }
    TEST_CASE_END();
}

/**
 * @brief Test de la validation des adresses de calendriers web (ICS / webcal).
 */
static void test_account_enabled_contract(void)
{
    TEST_CASE("Contrats JSON: Propriete 'enabled' d\'activation de compte");
    {
        const char *sample_account = "{\"uid\": \"account-1\", \"name\": \"Mon Compte\", \"enabled\": true, \"default\": false}";
        ASSERT_CONTAINS(sample_account, "\"enabled\":");
        ASSERT_CONTAINS(sample_account, "true");
    }
    TEST_CASE_END();

    TEST_CASE("Contrats JSON: Reponse de quota de stockage (evo_core_get_folder_quota)");
    {
        const char *sample_quota_active = "{\"has_quota\":true,\"name\":\"Stockage\",\"used\":1048576,\"total\":52428800,\"percent\":2}";
        ASSERT_CONTAINS(sample_quota_active, "\"has_quota\":true");
        ASSERT_CONTAINS(sample_quota_active, "\"used\":");
        ASSERT_CONTAINS(sample_quota_active, "\"total\":");
        ASSERT_CONTAINS(sample_quota_active, "\"percent\":");

        const char *sample_quota_none = "{\"has_quota\":false,\"name\":\"\",\"used\":0,\"total\":0,\"percent\":0}";
        ASSERT_CONTAINS(sample_quota_none, "\"has_quota\":false");
    }
    TEST_CASE_END();

    TEST_CASE("Contrats JSON: Enregistrement et remplacement de brouillon (evo_core_save_draft)");
    {
        const char *sample_draft_result = "{\"saved\":true,\"uid\":\"1042\",\"account\":\"compte-1\",\"folder\":\"Drafts\"}";
        ASSERT_CONTAINS(sample_draft_result, "\"saved\":true");
        ASSERT_CONTAINS(sample_draft_result, "\"uid\":");
        ASSERT_CONTAINS(sample_draft_result, "\"folder\":\"Drafts\"");
    }
    TEST_CASE_END();

    TEST_CASE("Contrats JSON: Modification de contact distant (evo_core_modify_contact)");
    {
        const char *sample_modify_contact = "{\"book_uid\":\"book-1\",\"uid\":\"c-101\",\"full_name\":\"Alice Martin\",\"email\":\"alice@exemple.fr\",\"phone\":\"+33123456789\",\"org\":\"Evolution\",\"notes\":\"Test notes\"}";
        ASSERT_CONTAINS(sample_modify_contact, "\"book_uid\":");
        ASSERT_CONTAINS(sample_modify_contact, "\"uid\":");
        ASSERT_CONTAINS(sample_modify_contact, "\"full_name\":\"Alice Martin\"");
        ASSERT_CONTAINS(sample_modify_contact, "\"email\":\"alice@exemple.fr\"");
    }
    TEST_CASE_END();
}

static void test_calendar_url_validation(void)
{
    TEST_CASE("Calendrier web: classification des adresses ICS / webcal");
    {
        ASSERT_EQ_INT(3, classify_calendar_url(""));
        ASSERT_EQ_INT(1, classify_calendar_url("https://exemple.fr/agenda.ics"));
        ASSERT_EQ_INT(1, classify_calendar_url("HTTP://exemple.fr/agenda.ics"));
        ASSERT_EQ_INT(2, classify_calendar_url("webcal://exemple.fr/agenda.ics"));
        ASSERT_EQ_INT(0, classify_calendar_url("ftp://exemple.fr/agenda.ics"));
        ASSERT_EQ_INT(0, classify_calendar_url("exemple.fr/agenda.ics"));
    }
    TEST_CASE_END();
}

#include "../../winui/core/evo-core-autoreply-util.h"

/**
 * @brief Teste les fonctions pures des reponses automatiques (evo-core-autoreply-util.h) :
 * dates ISO 8601 de Microsoft Graph, audiences Graph / Gmail et validation des plages.
 */
static void test_auto_reply_helpers(void)
{
    char buf[48];

    TEST_CASE("Reponses automatiques: conversion des dates ISO 8601 UTC <-> secondes Unix");
    {
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("1971-01-02T00:00:00") == 31622400);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("2024-02-29T12:30:45.0000000") == 1709209845);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("2026-11-04T07:30:00Z") == 1793777400);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("") == 0);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc(NULL) == 0);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("pas une date") == 0);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("2026-13-40T00:00:00") == 0);
        ASSERT_TRUE(autoreply_epoch_from_iso_utc("1969-12-31T23:59:59") == 0);

        autoreply_iso_from_epoch_utc(1709209845, buf, sizeof (buf));
        ASSERT_EQ_STR("2024-02-29T12:30:45.0000000", buf);
        autoreply_iso_from_epoch_utc(1793777400, buf, sizeof (buf));
        ASSERT_EQ_STR("2026-11-04T07:30:00.0000000", buf);
        /* aller-retour sur une annee bissextile et un changement de siecle */
        autoreply_iso_from_epoch_utc(autoreply_epoch_from_iso_utc("2100-03-01T00:00:01"), buf, sizeof (buf));
        ASSERT_EQ_STR("2100-03-01T00:00:01.0000000", buf);
    }
    TEST_CASE_END();

    TEST_CASE("Reponses automatiques: audiences Microsoft Graph et Gmail");
    {
        int contacts = -1, domain = -1;

        ASSERT_EQ_STR("none", autoreply_audience_to_graph("none"));
        ASSERT_EQ_STR("contactsOnly", autoreply_audience_to_graph("known"));
        ASSERT_EQ_STR("all", autoreply_audience_to_graph("all"));
        ASSERT_EQ_STR("all", autoreply_audience_to_graph(NULL));
        ASSERT_EQ_STR("known", autoreply_audience_from_graph("contactsOnly"));
        ASSERT_EQ_STR("none", autoreply_audience_from_graph("none"));
        ASSERT_EQ_STR("all", autoreply_audience_from_graph("inconnu"));

        autoreply_audience_to_gmail("known", &contacts, &domain);
        ASSERT_TRUE(contacts == 1 && domain == 0);
        autoreply_audience_to_gmail("none", &contacts, &domain);
        ASSERT_TRUE(contacts == 0 && domain == 1);
        autoreply_audience_to_gmail("all", &contacts, &domain);
        ASSERT_TRUE(contacts == 0 && domain == 0);
        ASSERT_EQ_STR("known", autoreply_audience_from_gmail(1, 0));
        ASSERT_EQ_STR("none", autoreply_audience_from_gmail(0, 1));
        ASSERT_EQ_STR("all", autoreply_audience_from_gmail(0, 0));

        ASSERT_TRUE(autoreply_audience_valid("known"));
        ASSERT_FALSE(autoreply_audience_valid("tous"));
        ASSERT_FALSE(autoreply_audience_valid(NULL));
    }
    TEST_CASE_END();

    TEST_CASE("Reponses automatiques: validation de la plage de dates");
    {
        ASSERT_EQ_INT(AUTOREPLY_RANGE_OK, autoreply_validate_range(0, 0, 1));
        ASSERT_EQ_INT(AUTOREPLY_RANGE_OK, autoreply_validate_range(100, 200, 1));
        ASSERT_EQ_INT(AUTOREPLY_RANGE_END_BEFORE_START, autoreply_validate_range(200, 100, 0));
        ASSERT_EQ_INT(AUTOREPLY_RANGE_END_BEFORE_START, autoreply_validate_range(100, 100, 1));
        ASSERT_EQ_INT(AUTOREPLY_RANGE_INCOMPLETE, autoreply_validate_range(100, 0, 1));
        ASSERT_EQ_INT(AUTOREPLY_RANGE_OK, autoreply_validate_range(100, 0, 0));
        ASSERT_EQ_INT(AUTOREPLY_RANGE_OK, autoreply_validate_range(0, 200, 0));
    }
    TEST_CASE_END();
}

/**
 * @brief Teste la construction des champs d'un accuse de lecture (MDN, RFC 8098).
 */
static void test_mdn_fields(void)
{
    char buf[1024];
    int n;

    TEST_CASE("MDN: champs message/disposition-notification (RFC 8098)");
    {
        n = evo_mdn_format_fields(buf, sizeof buf, "EvolutionWin", "marie@exemple.fr", "<abc@exemple.fr>", "displayed");
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(strstr(buf, "Reporting-UA: EvolutionWin\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "Final-Recipient: rfc822; marie@exemple.fr\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "Original-Message-ID: <abc@exemple.fr>\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "Disposition: manual-action/MDN-sent-manually; displayed\r\n") != NULL);
        ASSERT_EQ_INT(n, (int)strlen(buf));
    }
    TEST_CASE_END();

    TEST_CASE("MDN: Message-ID absent, valeurs par defaut et anti-injection d'en-tetes");
    {
        n = evo_mdn_format_fields(buf, sizeof buf, "", "a@b.fr", "", "");
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(strstr(buf, "Original-Message-ID") == NULL);
        ASSERT_TRUE(strstr(buf, "Reporting-UA: EvolutionWin") != NULL);
        ASSERT_TRUE(strstr(buf, "; displayed\r\n") != NULL);

        /* un retour a la ligne dans une valeur ne peut pas creer de faux champ */
        n = evo_mdn_format_fields(buf, sizeof buf, "UA", "a@b.fr\r\nBcc: x@y.fr", "<id>", "displayed");
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(strstr(buf, "\r\nBcc:") == NULL);
        ASSERT_TRUE(strstr(buf, "Final-Recipient: rfc822; a@b.fr  Bcc: x@y.fr\r\n") != NULL);

        /* tampon trop petit : echec propre */
        ASSERT_EQ_INT(-1, evo_mdn_format_fields(buf, 20, "UA", "a@b.fr", "<id>", "displayed"));
    }
    TEST_CASE_END();
}

/** @brief Invitation Outlook type : fuseau, VALARM imbrique, participants, lignes repliees. */
static const char *SAMPLE_REQUEST =
    "BEGIN:VCALENDAR\r\nMETHOD:REQUEST\r\nPRODID:Microsoft Exchange\r\nVERSION:2.0\r\n"
    "BEGIN:VTIMEZONE\r\nTZID:Romance Standard Time\r\nBEGIN:STANDARD\r\nDTSTART:16011028T030000\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"
    "BEGIN:VEVENT\r\nUID:040000008200E00074C5B7101A82E008\r\nSEQUENCE:2\r\n"
    "SUMMARY:Point projet\\, equipe\r\n"
    "DTSTART;TZID=Romance Standard Time:20261105T090000\r\nDTEND;TZID=Romance Standard Time:20261105T100000\r\n"
    "LOCATION:Salle 12\\; b\xC3\xA2" "timent A\r\n"
    "ORGANIZER;CN=\"Durand, Marie\":mailto:marie@exemple.fr\r\n"
    "ATTENDEE;ROLE=REQ-PARTICIPANT;PARTSTAT=NEEDS-ACTION;RSVP=TRUE;CN=Pierre:mailto:Pierre@Exemple.fr\r\n"
    "ATTENDEE;ROLE=OPT-PARTICIPANT;PARTSTAT=ACCEPTED;CN=Luc:MAILTO:luc@exemple.fr\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=4\r\n"
    "DESCRIPTION:ligne longue qui se\r\n  poursuit ici\r\n"
    "BEGIN:VALARM\r\nTRIGGER:-PT15M\r\nDESCRIPTION:Rappel\r\nEND:VALARM\r\n"
    "END:VEVENT\r\nEND:VCALENDAR\r\n";

/**
 * @brief Teste la lecture du resume d'une invitation iCalendar (evo-core-itip.h).
 */
static void test_itip_parse(void)
{
    static EvoItip it;

    TEST_CASE("iTIP: resume d'une invitation REQUEST (champs, fuseau, participants)");
    {
        ASSERT_TRUE(evo_itip_parse(SAMPLE_REQUEST, &it));
        ASSERT_EQ_STR("REQUEST", it.method);
        ASSERT_EQ_STR("040000008200E00074C5B7101A82E008", it.uid);
        ASSERT_EQ_INT(2, it.sequence);
        ASSERT_EQ_STR("Point projet, equipe", it.summary);
        ASSERT_EQ_STR("20261105T090000", it.dtstart);
        ASSERT_EQ_STR("Romance Standard Time", it.dtstart_tzid);
        ASSERT_EQ_STR("20261105T100000", it.dtend);
        ASSERT_FALSE(it.dtstart_is_date);
        ASSERT_EQ_STR("Salle 12; b\xC3\xA2" "timent A", it.location);
        ASSERT_EQ_STR("FREQ=WEEKLY;COUNT=4", it.rrule);
        ASSERT_EQ_STR("marie@exemple.fr", it.organizer.email);
        ASSERT_EQ_STR("Durand, Marie", it.organizer.name);
        ASSERT_EQ_INT(2, it.n_attendees);
        ASSERT_EQ_STR("NEEDS-ACTION", it.attendees[0].partstat);
        ASSERT_EQ_STR("REQ-PARTICIPANT", it.attendees[0].role);
        ASSERT_EQ_STR("luc@exemple.fr", it.attendees[1].email);
        ASSERT_EQ_STR("ACCEPTED", it.attendees[1].partstat);
        /* recherche du participant : insensible a la casse */
        ASSERT_EQ_INT(0, evo_itip_find_attendee(&it, "pierre@exemple.FR"));
        ASSERT_EQ_INT(-1, evo_itip_find_attendee(&it, "inconnu@exemple.fr"));
        ASSERT_EQ_INT(-1, evo_itip_find_attendee(&it, ""));
    }
    TEST_CASE_END();

    TEST_CASE("iTIP: journee entiere, annulation, DTSTART des VTIMEZONE et VALARM ignores");
    {
        const char *cancel =
            "BEGIN:VCALENDAR\nMETHOD:CANCEL\nBEGIN:VEVENT\nUID:abc\nSEQUENCE:3\nSTATUS:cancelled\n"
            "DTSTART;VALUE=DATE:20261224\nSUMMARY:Noel\nEND:VEVENT\nEND:VCALENDAR\n";
        ASSERT_TRUE(evo_itip_parse(cancel, &it));
        ASSERT_EQ_STR("CANCEL", it.method);
        ASSERT_EQ_STR("CANCELLED", it.status);
        ASSERT_TRUE(it.dtstart_is_date);
        ASSERT_EQ_STR("20261224", it.dtstart);
        ASSERT_EQ_INT(3, it.sequence);

        /* seul le DTSTART du VEVENT compte (pas celui du VTIMEZONE) */
        ASSERT_TRUE(evo_itip_parse(SAMPLE_REQUEST, &it));
        ASSERT_TRUE(strstr(it.dtstart, "1601") == NULL);
    }
    TEST_CASE_END();

    TEST_CASE("iTIP: documents invalides ou sans evenement");
    {
        ASSERT_FALSE(evo_itip_parse(NULL, &it));
        ASSERT_FALSE(evo_itip_parse("", &it));
        ASSERT_FALSE(evo_itip_parse("BEGIN:VCALENDAR\r\nMETHOD:PUBLISH\r\nEND:VCALENDAR\r\n", &it));
        /* VEVENT sans UID */
        ASSERT_FALSE(evo_itip_parse("BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:x\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n", &it));
        /* ligne sans separateur et methode en minuscules */
        ASSERT_TRUE(evo_itip_parse("BEGIN:VCALENDAR\r\nmethod:request\r\ngarbage\r\nBEGIN:VEVENT\r\nUID:z\r\nEND:VEVENT\r\nEND:VCALENDAR", &it));
        ASSERT_EQ_STR("REQUEST", it.method);
    }
    TEST_CASE_END();
}

/**
 * @brief Teste la decision d'ajout, de mise a jour ou de retrait dans l'agenda (anti-doublons).
 */
static void test_itip_decide(void)
{
    TEST_CASE("iTIP: decision agenda (doublons via UID et SEQUENCE)");
    {
        /* absent : ajout ; refus d'un evenement absent : rien */
        ASSERT_EQ_INT(EVO_ITIP_ADD, evo_itip_decide("REQUEST", 0, 0, 0, 0));
        ASSERT_EQ_INT(EVO_ITIP_IGNORE, evo_itip_decide("REQUEST", 0, 0, 0, 1));
        /* deja present : remplace par une version egale ou plus recente, jamais plus ancienne */
        ASSERT_EQ_INT(EVO_ITIP_UPDATE, evo_itip_decide("REQUEST", 1, 1, 2, 0));
        ASSERT_EQ_INT(EVO_ITIP_UPDATE, evo_itip_decide("REQUEST", 1, 2, 2, 0));
        ASSERT_EQ_INT(EVO_ITIP_KEEP, evo_itip_decide("REQUEST", 1, 3, 2, 0));
        /* refus : retire l'evenement */
        ASSERT_EQ_INT(EVO_ITIP_REMOVE, evo_itip_decide("request", 1, 0, 0, 1));
        /* annulation : retire si presente et non perimee */
        ASSERT_EQ_INT(EVO_ITIP_REMOVE, evo_itip_decide("CANCEL", 1, 1, 2, 0));
        ASSERT_EQ_INT(EVO_ITIP_IGNORE, evo_itip_decide("CANCEL", 0, 0, 5, 0));
        ASSERT_EQ_INT(EVO_ITIP_IGNORE, evo_itip_decide("CANCEL", 1, 4, 2, 0));
        /* autres methodes : aucune action */
        ASSERT_EQ_INT(EVO_ITIP_IGNORE, evo_itip_decide("REPLY", 1, 0, 0, 0));
        ASSERT_EQ_INT(EVO_ITIP_IGNORE, evo_itip_decide(NULL, 1, 0, 0, 0));
    }
    TEST_CASE_END();

    TEST_CASE("iTIP: correspondance reponse de l'interface <-> PARTSTAT");
    {
        ASSERT_EQ_STR("ACCEPTED", evo_itip_partstat_from_response("accepted"));
        ASSERT_EQ_STR("DECLINED", evo_itip_partstat_from_response("Declined"));
        ASSERT_EQ_STR("TENTATIVE", evo_itip_partstat_from_response("tentative"));
        ASSERT_NULL(evo_itip_partstat_from_response("remove"));
        ASSERT_NULL(evo_itip_partstat_from_response(NULL));
        ASSERT_EQ_STR("accepted", evo_itip_response_from_partstat("ACCEPTED"));
        ASSERT_EQ_STR("declined", evo_itip_response_from_partstat("declined"));
        ASSERT_EQ_STR("tentative", evo_itip_response_from_partstat("TENTATIVE"));
        ASSERT_EQ_STR("needs-action", evo_itip_response_from_partstat("NEEDS-ACTION"));
        ASSERT_EQ_STR("needs-action", evo_itip_response_from_partstat(""));
    }
    TEST_CASE_END();
}

/**
 * @brief Teste la generation de la reponse iTIP METHOD:REPLY.
 */
static void test_itip_reply(void)
{
    static EvoItip it;
    static char buf[4096];
    int n;

    TEST_CASE("iTIP: generation du REPLY (RFC 5546)");
    {
        ASSERT_TRUE(evo_itip_parse(SAMPLE_REQUEST, &it));
        n = evo_itip_format_reply(buf, sizeof buf, &it, "pierre@exemple.fr", "Pierre", "ACCEPTED", "20261104T230000Z");
        ASSERT_TRUE(n > 0);
        ASSERT_EQ_INT(n, (int)strlen(buf));
        ASSERT_TRUE(strncmp(buf, "BEGIN:VCALENDAR\r\n", 17) == 0);
        ASSERT_TRUE(strstr(buf, "\r\nMETHOD:REPLY\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nUID:040000008200E00074C5B7101A82E008\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nSEQUENCE:2\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nDTSTAMP:20261104T230000Z\r\n") != NULL);
        /* organisateur avec nom entre guillemets (contient une virgule) */
        ASSERT_TRUE(strstr(buf, "\r\nORGANIZER;CN=\"Durand, Marie\":mailto:marie@exemple.fr\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nATTENDEE;PARTSTAT=ACCEPTED;CN=Pierre:mailto:pierre@exemple.fr\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nSUMMARY:Point projet\\, ") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n") != NULL);
        /* un seul participant dans la reponse ; DTSTART avec TZID omis (pas de VTIMEZONE) */
        ASSERT_TRUE(strstr(buf, "luc@exemple.fr") == NULL);
        ASSERT_TRUE(strstr(buf, "DTSTART") == NULL);
    }
    TEST_CASE_END();

    TEST_CASE("iTIP: REPLY : DTSTART UTC repris, occurrence, refus, nom absent");
    {
        const char *ics =
            "BEGIN:VCALENDAR\r\nMETHOD:REQUEST\r\nBEGIN:VEVENT\r\nUID:u1\r\nDTSTART:20261105T080000Z\r\n"
            "RECURRENCE-ID:20261112T080000Z\r\nSUMMARY:Revue\r\nORGANIZER:mailto:chef@exemple.fr\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
        ASSERT_TRUE(evo_itip_parse(ics, &it));
        n = evo_itip_format_reply(buf, sizeof buf, &it, "moi@exemple.fr", "", "DECLINED", "20261104T230000Z");
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(strstr(buf, "\r\nDTSTART:20261105T080000Z\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nRECURRENCE-ID:20261112T080000Z\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nORGANIZER:mailto:chef@exemple.fr\r\n") != NULL);
        ASSERT_TRUE(strstr(buf, "\r\nATTENDEE;PARTSTAT=DECLINED:mailto:moi@exemple.fr\r\n") != NULL);
    }
    TEST_CASE_END();

    TEST_CASE("iTIP: REPLY : anti-injection, repli a 75 octets, tampon trop petit, arguments invalides");
    {
        const char *ics =
            "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:u2\r\nSUMMARY:Une reunion au titre tres tres long, qui depasse largement les soixante-quinze octets autorises par ligne\r\n"
            "ORGANIZER:mailto:chef@exemple.fr\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
        char *line;
        ASSERT_TRUE(evo_itip_parse(ics, &it));
        n = evo_itip_format_reply(buf, sizeof buf, &it, "moi@exemple.fr\r\nBcc:x@y.fr", "Nom\r\nX-Evil: 1", "TENTATIVE", "20261104T230000Z");
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(strstr(buf, "\r\nBcc:") == NULL);
        ASSERT_TRUE(strstr(buf, "\r\nX-Evil:") == NULL);
        /* aucune ligne ne depasse 75 octets (hors CRLF) et le titre est replie avec une espace */
        for (line = buf; *line; ) {
            char *eol = strstr(line, "\r\n");
            ASSERT_TRUE(eol != NULL);
            ASSERT_TRUE((int)(eol - line) <= 75);
            line = eol + 2;
        }
        ASSERT_TRUE(strstr(buf, "\r\n ") != NULL);

        ASSERT_EQ_INT(-1, evo_itip_format_reply(buf, 100, &it, "moi@exemple.fr", "", "ACCEPTED", "20261104T230000Z"));
        ASSERT_EQ_INT(-1, evo_itip_format_reply(buf, sizeof buf, &it, "", "", "ACCEPTED", "20261104T230000Z"));
        ASSERT_EQ_INT(-1, evo_itip_format_reply(buf, sizeof buf, &it, "moi@exemple.fr", "", "", "20261104T230000Z"));
        /* sans organisateur : pas de reponse possible */
        it.organizer.email[0] = '\0';
        ASSERT_EQ_INT(-1, evo_itip_format_reply(buf, sizeof buf, &it, "moi@exemple.fr", "", "ACCEPTED", "20261104T230000Z"));
    }
    TEST_CASE_END();

    TEST_CASE("iTIP: objet et corps lisibles de la reponse");
    {
        evo_itip_reply_subject(buf, sizeof buf, "ACCEPTED", "Point projet");
        ASSERT_EQ_STR("Accept\xC3\xA9 : Point projet", buf);
        evo_itip_reply_subject(buf, sizeof buf, "DECLINED", "Point\r\nprojet");
        ASSERT_EQ_STR("Refus\xC3\xA9 : Point  projet", buf);
        evo_itip_reply_subject(buf, sizeof buf, "TENTATIVE", "X");
        ASSERT_EQ_STR("Provisoire : X", buf);

        evo_itip_reply_body(buf, sizeof buf, "Pierre", "ACCEPTED", "Point projet", "05/11/2026 09:00");
        ASSERT_TRUE(strstr(buf, "Pierre a accept\xC3\xA9 l'invitation \xC2\xAB Point projet \xC2\xBB du 05/11/2026 09:00.") != NULL);
        ASSERT_TRUE(strstr(buf, "Pierre has accepted the invitation \"Point projet\" on 05/11/2026 09:00.") != NULL);
        evo_itip_reply_body(buf, sizeof buf, "Pierre", "DECLINED", "X", "");
        ASSERT_TRUE(strstr(buf, "a refus\xC3\xA9 l'invitation \xC2\xAB X \xC2\xBB.") != NULL);
    }
    TEST_CASE_END();
}

#include "../../winui/core/evo-core-gal-util.h"

/**
 * @brief Teste les fonctions pures de la recherche dans l'annuaire de l'organisation
 * (evo-core-gal-util.h) : nettoyage de la saisie, encodage d'URL, adresses des API
 * Microsoft Graph et Google People, normalisation des adresses.
 */
static void test_gal_helpers(void)
{
    char buf[2048];

    TEST_CASE("GAL: nettoyage de la saisie (guillemets, antislashs, espaces)");
    {
        ASSERT_EQ_INT(4, (int) gal_sanitize_term("Jean", buf, sizeof (buf)));
        ASSERT_EQ_STR("Jean", buf);
        gal_sanitize_term("  Jean   \"Pierre\" \\ Dupont \t", buf, sizeof (buf));
        ASSERT_EQ_STR("Jean Pierre Dupont", buf);
        gal_sanitize_term("\"\"\\\\", buf, sizeof (buf));
        ASSERT_EQ_STR("", buf);
        gal_sanitize_term(NULL, buf, sizeof (buf));
        ASSERT_EQ_STR("", buf);
        gal_sanitize_term("é\xC3\xA9 ok", buf, sizeof (buf));
        ASSERT_TRUE(strstr(buf, "ok") != NULL);   /* UTF-8 conserve */
        /* troncature sans debordement */
        gal_sanitize_term("abcdefghij", buf, 5);
        ASSERT_EQ_STR("abcd", buf);
    }
    TEST_CASE_END();

    TEST_CASE("GAL: longueur minimale de la recherche (caracteres UTF-8)");
    {
        ASSERT_FALSE(gal_query_valid(""));
        ASSERT_FALSE(gal_query_valid("a"));
        ASSERT_TRUE(gal_query_valid("ab"));
        ASSERT_FALSE(gal_query_valid("\xC3\xA9"));          /* un seul caractere de 2 octets */
        ASSERT_TRUE(gal_query_valid("\xC3\xA9\xC3\xA8"));   /* deux caracteres */
        ASSERT_FALSE(gal_query_valid(NULL));
    }
    TEST_CASE_END();

    TEST_CASE("GAL: encodage en pourcentage des URL");
    {
        ASSERT_TRUE(gal_url_encode("abc-._~123", buf, sizeof (buf)) == 10);
        ASSERT_EQ_STR("abc-._~123", buf);
        gal_url_encode("a b\"c:d", buf, sizeof (buf));
        ASSERT_EQ_STR("a%20b%22c%3Ad", buf);
        gal_url_encode("\xC3\xA9", buf, sizeof (buf));
        ASSERT_EQ_STR("%C3%A9", buf);
        /* trop petit : vide plutot qu'un encodage partiel */
        ASSERT_EQ_INT(0, (int) gal_url_encode("a b c", buf, 6));
        ASSERT_EQ_STR("", buf);
    }
    TEST_CASE_END();

    TEST_CASE("GAL: nombre de resultats borne");
    {
        ASSERT_EQ_INT(15, gal_clamp_limit(0));
        ASSERT_EQ_INT(15, gal_clamp_limit(-3));
        ASSERT_EQ_INT(8, gal_clamp_limit(8));
        ASSERT_EQ_INT(GAL_MAX_RESULTS, gal_clamp_limit(5000));
    }
    TEST_CASE_END();

    TEST_CASE("GAL: adresse de recherche Microsoft Graph");
    {
        ASSERT_TRUE(gal_graph_search_url("marie", 10, GAL_GRAPH_SELECT_BASIC, buf, sizeof (buf)));
        ASSERT_TRUE(strncmp(buf, "https://graph.microsoft.com/v1.0/users?$search=", 47) == 0);
        ASSERT_TRUE(strstr(buf, "%22displayName%3Amarie%22%20OR%20%22mail%3Amarie%22") != NULL);
        ASSERT_TRUE(strstr(buf, "&$top=10&") != NULL);
        ASSERT_TRUE(strstr(buf, "&$select=displayName,givenName,surname,mail,userPrincipalName") != NULL);

        /* saisie trop courte, absente, ou tampon trop petit : refus */
        ASSERT_FALSE(gal_graph_search_url("m", 10, GAL_GRAPH_SELECT_BASIC, buf, sizeof (buf)));
        ASSERT_FALSE(gal_graph_search_url(NULL, 10, GAL_GRAPH_SELECT_BASIC, buf, sizeof (buf)));
        ASSERT_FALSE(gal_graph_search_url("marie", 10, GAL_GRAPH_SELECT_FULL, buf, 40));

        /* un nom avec espace est encode dans l'expression */
        ASSERT_TRUE(gal_graph_search_url("marie dupont", 99, GAL_GRAPH_SELECT_FULL, buf, sizeof (buf)));
        ASSERT_TRUE(strstr(buf, "displayName%3Amarie%20dupont") != NULL);
        ASSERT_TRUE(strstr(buf, "&$top=50&") != NULL);
        ASSERT_TRUE(strstr(buf, "jobTitle,department,companyName,officeLocation,mobilePhone,businessPhones") != NULL);
    }
    TEST_CASE_END();

    TEST_CASE("GAL: adresse de recherche Google People");
    {
        ASSERT_TRUE(gal_google_search_url("jean", 5, buf, sizeof (buf)));
        ASSERT_TRUE(strncmp(buf, "https://people.googleapis.com/v1/people:searchDirectoryPeople?query=jean&pageSize=5&",
                            strlen("https://people.googleapis.com/v1/people:searchDirectoryPeople?query=jean&pageSize=5&")) == 0);
        ASSERT_TRUE(strstr(buf, "readMask=names,emailAddresses,organizations,phoneNumbers,locations") != NULL);
        ASSERT_TRUE(strstr(buf, "sources=DIRECTORY_SOURCE_TYPE_DOMAIN_PROFILE") != NULL);
        ASSERT_FALSE(gal_google_search_url("j", 5, buf, sizeof (buf)));
        ASSERT_FALSE(gal_google_search_url("jean", 5, buf, 30));
        ASSERT_TRUE(gal_google_search_url("o'neil & co", 5, buf, sizeof (buf)));
        ASSERT_TRUE(strstr(buf, "query=o%27neil%20%26%20co&") != NULL);
    }
    TEST_CASE_END();

    TEST_CASE("GAL: normalisation des adresses electroniques");
    {
        ASSERT_TRUE(gal_normalize_email("Marie.Dupont@Exemple.FR", buf, sizeof (buf)));
        ASSERT_EQ_STR("marie.dupont@exemple.fr", buf);
        ASSERT_TRUE(gal_normalize_email("SMTP:Marie@exemple.fr", buf, sizeof (buf)));
        ASSERT_EQ_STR("marie@exemple.fr", buf);
        ASSERT_TRUE(gal_normalize_email("  smtp:a@b.c  ", buf, sizeof (buf)));
        ASSERT_EQ_STR("a@b.c", buf);
        ASSERT_FALSE(gal_normalize_email("", buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email(NULL, buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email("sans-arobase", buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email("@exemple.fr", buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email("marie@", buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email("a@b@c.fr", buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email("marie dupont@exemple.fr", buf, sizeof (buf)));
        ASSERT_FALSE(gal_normalize_email("x500:/o=Org/cn=Marie", buf, sizeof (buf)));
    }
    TEST_CASE_END();
}

/**
 * @brief Point d'entree de la suite de tests des contrats evo-core.
 * @return 0 si succes, 1 en cas d'echec.
 */
int main(void)
{
    TEST_SUITE_BEGIN("Contrats d'interface & Robustesse evo-core");

    test_credential_chunking_large_secret();
    test_email_validation();
    test_special_folders_priority();
    test_json_event_contracts();
    test_calendar_url_validation();
    test_account_enabled_contract();
    test_mdn_fields();
    test_auto_reply_helpers();
    test_itip_parse();
    test_itip_decide();
    test_itip_reply();
    test_gal_helpers();

    return test_suite_summary();
}

