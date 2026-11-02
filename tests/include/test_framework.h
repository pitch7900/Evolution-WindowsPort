/**
 * @file test_framework.h
 * @brief Micro-cadre de tests unitaires léger et autonome en C/C++ pour Evolution-WindowsPort.
 *
 * Ce fichier d'en-tête fournit les macros nécessaires pour exécuter des tests unitaires
 * sans dépendance externe (sans GoogleTest ni Boost), compatible avec MSVC sur Windows 11.
 * Il gère le décompte des assertions, le rapport d'erreurs détaillé et les codes de sortie.
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compteurs globaux pour la suite de tests */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_current_test_failed = 0;
static const char *g_current_suite = "General";

/**
 * @brief Initialise une nouvelle suite de tests.
 * @param name Nom de la suite de tests.
 */
static inline void test_suite_begin(const char *name)
{
    g_current_suite = name;
    printf("\n======================================================\n");
    printf("  SUITE DE TESTS : %s\n", name);
    printf("======================================================\n");
    fflush(stdout);
}

/**
 * @brief Déclare le début d'un cas de test individuel.
 * @param name Nom descriptif du test.
 */
static inline void test_case_begin(const char *name)
{
    g_tests_run++;
    g_current_test_failed = 0;
    printf("  [TEST] %s ... ", name);
    fflush(stdout);
}

/**
 * @brief Clôture un cas de test et affiche son résultat.
 */
static inline void test_case_end(void)
{
    if (g_current_test_failed == 0) {
        g_tests_passed++;
        printf("SUCCES\n");
    } else {
        g_tests_failed++;
        printf("ECHEC\n");
    }
    fflush(stdout);
}

/**
 * @brief Affiche le bilan de la suite de tests et retourne le code de sortie.
 * @return 0 si tous les tests ont réussi, 1 si au moins un test a échoué.
 */
static inline int test_suite_summary(void)
{
    printf("------------------------------------------------------\n");
    printf("  Resultats : %d tests executes, %d reussis, %d echoues.\n",
           g_tests_run, g_tests_passed, g_tests_failed);
    printf("======================================================\n\n");
    fflush(stdout);
    return (g_tests_failed == 0) ? 0 : 1;
}

#ifdef __cplusplus
}
#endif

/* --- Macros d'assertion --- */

#define TEST_SUITE_BEGIN(name) test_suite_begin(name)

#define TEST_CASE(name) do { \
    test_case_begin(name);

#define TEST_CASE_END() \
    test_case_end(); \
} while (0);

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("\n    -> Echec assertion (ASSERT_TRUE) : %s\n       Fichier : %s, Ligne : %d\n", \
               #condition, __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#define ASSERT_FALSE(condition) do { \
    if (condition) { \
        printf("\n    -> Echec assertion (ASSERT_FALSE) : %s\n       Fichier : %s, Ligne : %d\n", \
               #condition, __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#define ASSERT_EQ_INT(expected, actual) do { \
    long long exp_val = (long long)(expected); \
    long long act_val = (long long)(actual); \
    if (exp_val != act_val) { \
        printf("\n    -> Echec assertion (ASSERT_EQ_INT) : Attendu %lld, Obtenu %lld\n       Fichier : %s, Ligne : %d\n", \
               exp_val, act_val, __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#define ASSERT_EQ_STR(expected, actual) do { \
    const char *exp_s = (const char *)(expected); \
    const char *act_s = (const char *)(actual); \
    if (exp_s == NULL && act_s == NULL) { \
        /* Les deux sont nuls : egalite */ \
    } else if (exp_s == NULL || act_s == NULL || strcmp(exp_s, act_s) != 0) { \
        printf("\n    -> Echec assertion (ASSERT_EQ_STR) :\n       Attendu : \"%s\"\n       Obtenu  : \"%s\"\n       Fichier : %s, Ligne : %d\n", \
               exp_s ? exp_s : "(null)", act_s ? act_s : "(null)", __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf("\n    -> Echec assertion (ASSERT_NOT_NULL) : Pointeur nul\n       Fichier : %s, Ligne : %d\n", \
               __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("\n    -> Echec assertion (ASSERT_NULL) : Pointeur non nul\n       Fichier : %s, Ligne : %d\n", \
               __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#define ASSERT_CONTAINS(haystack, needle) do { \
    const char *hay = (const char *)(haystack); \
    const char *ndl = (const char *)(needle); \
    if (hay == NULL || ndl == NULL || strstr(hay, ndl) == NULL) { \
        printf("\n    -> Echec assertion (ASSERT_CONTAINS) :\n       Chaine  : \"%s\"\n       Ne contient pas : \"%s\"\n       Fichier : %s, Ligne : %d\n", \
               hay ? hay : "(null)", ndl ? ndl : "(null)", __FILE__, __LINE__); \
        g_current_test_failed = 1; \
    } \
} while (0)

#endif /* TEST_FRAMEWORK_H */

