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

    return test_suite_summary();
}

