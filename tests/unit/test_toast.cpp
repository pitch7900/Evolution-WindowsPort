/**
 * @file test_toast.cpp
 * @brief Tests unitaires des notifications système de courrier (ToastNotifications.h) : arguments d'un toast
 *        cliqué, actions rapides, composition des lignes d'un toast et intervalle de vérification du courrier.
 */
#include "../../winui/app/ToastNotifications.h"

#include <cstdio>

using namespace EvolutionWin::Toast;

static int g_failures = 0;
static int g_tests = 0;

#define CHECK(cond) do { ++g_tests; if (!(cond)) { ++g_failures; std::printf("ECHEC ligne %d : %s\n", __LINE__, #cond); } } while (0)

/** Classement de la valeur de l'argument « action ». */
static void test_classify()
{
    CHECK(ClassifyAction(L"delete") == Action::Delete);
    CHECK(ClassifyAction(L"flag") == Action::Flag);
    CHECK(ClassifyAction(L"dismiss") == Action::Dismiss);
    CHECK(ClassifyAction(L"event") == Action::Event);
    /* clic sur le corps du toast : pas d'action, ou valeur inconnue */
    CHECK(ClassifyAction(L"") == Action::Open);
    CHECK(ClassifyAction(L"inconnue") == Action::Open);
    /* sensible à la casse : pas d'action destructrice par accident */
    CHECK(ClassifyAction(L"Delete") == Action::Open);
    CHECK(ClassifyAction(L"DELETE") == Action::Open);
}

/** Décodage des arguments d'un toast de message et de ses boutons. */
static void test_parse_arguments()
{
    auto body = ParseArguments({ { L"account", L"acc1" }, { L"folder", L"INBOX" }, { L"uid", L"42" } });
    CHECK(body.action == Action::Open);
    CHECK(body.account == L"acc1" && body.folder == L"INBOX" && body.uid == L"42");
    CHECK(body.HasMessage());

    auto del = ParseArguments({ { L"action", L"delete" }, { L"account", L"a" }, { L"folder", L"INBOX/Sous" }, { L"uid", L"7" } });
    CHECK(del.action == Action::Delete && del.HasMessage());

    /* rappel de calendrier : pas de message désigné */
    auto event = ParseArguments({ { L"action", L"event" }, { L"calendar", L"cal" }, { L"uid", L"e1" } });
    CHECK(event.action == Action::Event && !event.HasMessage());

    /* message incomplet : jamais d'action sur un message mal désigné */
    CHECK(!ParseArguments({ { L"action", L"delete" }, { L"account", L"a" }, { L"uid", L"7" } }).HasMessage());
    CHECK(!ParseArguments({ { L"action", L"flag" }, { L"folder", L"INBOX" }, { L"uid", L"7" } }).HasMessage());
    CHECK(!ParseArguments({ { L"action", L"flag" }, { L"account", L"a" }, { L"folder", L"INBOX" } }).HasMessage());
    CHECK(!ParseArguments({}).HasMessage());

    /* clés inconnues ignorées, dernière valeur retenue */
    auto extra = ParseArguments({ { L"x", L"y" }, { L"uid", L"1" }, { L"uid", L"2" } });
    CHECK(extra.uid == L"2" && extra.account.empty());
}

/** Lignes du toast : titre, expéditeur, objet ; mode confidentiel. */
static void test_compose()
{
    auto single = ComposeNewMail(L"Nouveau message", L"Alice", L"Réunion demain", false, L"Ouvrez Evolution pour le lire.");
    CHECK(single.title == L"Nouveau message");
    CHECK(single.first == L"Alice");
    CHECK(single.second == L"Réunion demain");

    auto secret = ComposeNewMail(L"3 nouveaux messages", L"Alice", L"Réunion demain", true, L"Ouvrez Evolution pour les lire.");
    CHECK(secret.title == L"3 nouveaux messages");
    CHECK(secret.first == L"Ouvrez Evolution pour les lire.");
    CHECK(secret.second.empty());
    CHECK(secret.first.find(L"Alice") == std::wstring::npos);
    CHECK(secret.first.find(L"Réunion") == std::wstring::npos);

    /* objet vide : ligne omise par l'appelant */
    auto nosubject = ComposeNewMail(L"Nouveau message", L"Bob", L"", false, L"");
    CHECK(nosubject.first == L"Bob" && nosubject.second.empty());
}

/** Conversion du réglage « mail.sync-interval » (minutes) en secondes. */
static void test_refresh_seconds()
{
    CHECK(RefreshSeconds(L"1") == 60);
    CHECK(RefreshSeconds(L"5") == 300);
    CHECK(RefreshSeconds(L"15") == 900);
    CHECK(RefreshSeconds(L"30") == 1800);
    /* manuellement uniquement */
    CHECK(RefreshSeconds(L"0") == 0);
    /* valeurs invalides : défaut de 5 minutes */
    CHECK(RefreshSeconds(L"") == kDefaultRefreshSeconds);
    CHECK(RefreshSeconds(L"-1") == kDefaultRefreshSeconds);
    CHECK(RefreshSeconds(L"abc") == kDefaultRefreshSeconds);
    CHECK(RefreshSeconds(L"5 min") == kDefaultRefreshSeconds);
    CHECK(kDefaultRefreshSeconds == 300);
    /* plafonnée à 24 h, sans dépassement d'entier */
    CHECK(RefreshSeconds(L"1440") == 24u * 3600u);
    CHECK(RefreshSeconds(L"99999") == 24u * 3600u);
    CHECK(RefreshSeconds(L"99999999999999999999999") == 24u * 3600u);
}

int main()
{
    test_classify();
    test_parse_arguments();
    test_compose();
    test_refresh_seconds();
    std::printf("test_toast : %d verifications, %d echec(s)\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
