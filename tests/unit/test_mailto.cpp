/**
 * @file test_mailto.cpp
 * @brief Tests unitaires de l'analyse des liens mailto: et de la description de l'enregistrement
 *        système (Mailto.h).
 */
#include "../../winui/app/Mailto.h"

#include <cstdio>

using namespace EvolutionWin::Mailto;

static int g_failures = 0;
static int g_tests = 0;

#define CHECK(cond) do { ++g_tests; if (!(cond)) { ++g_failures; std::printf("ECHEC ligne %d : %s\n", __LINE__, #cond); } } while (0)

/** Adresse simple et schéma insensible à la casse. */
static void test_simple()
{
    Message m;
    CHECK(Parse(L"mailto:a@b.fr", m));
    CHECK(m.to == L"a@b.fr" && m.cc.empty() && m.subject.empty() && m.body.empty());
    CHECK(Parse(L"MAILTO:A@B.FR", m) && m.to == L"A@B.FR");
    CHECK(Parse(L"mailto:", m) && m.to.empty());
    CHECK(Parse(L"mailto://a@b.fr/", m) && m.to == L"a@b.fr");
    CHECK(!Parse(L"http://a@b.fr", m));
    CHECK(!Parse(L"", m));
    CHECK(IsMailtoUrl(L"MailTo:x") && !IsMailtoUrl(L"mail:x"));
}

/** Plusieurs destinataires et champs de la requête. */
static void test_fields()
{
    Message m;
    CHECK(Parse(L"mailto:a@b.fr,c@d.fr?cc=e@f.fr&bcc=g@h.fr&subject=Bonjour&body=Salut", m));
    CHECK(m.to == L"a@b.fr,c@d.fr");
    CHECK(m.cc == L"e@f.fr" && m.bcc == L"g@h.fr");
    CHECK(m.subject == L"Bonjour" && m.body == L"Salut");

    CHECK(Parse(L"mailto:a@b.fr?to=c@d.fr&TO=e@f.fr&Subject=S&inconnu=1&sansvaleur", m));
    CHECK(m.to == L"a@b.fr, c@d.fr, e@f.fr" && m.subject == L"S");

    CHECK(Parse(L"mailto:?to=a@b.fr&cc=c@d.fr", m) && m.to == L"a@b.fr" && m.cc == L"c@d.fr");
    CHECK(Parse(L"mailto:a@b.fr#ancre", m) && m.to == L"a@b.fr");
}

/** Décodage des %XX en UTF-8, + conservé, caractères non valides remplacés. */
static void test_decoding()
{
    Message m;
    CHECK(Parse(L"mailto:a%40b.fr?subject=Caf%C3%A9%20%E2%82%AC%2B1+2", m));
    CHECK(m.to == L"a@b.fr");
    CHECK(m.subject == L"Caf\u00e9 \u20ac+1+2");

    /* hors BMP : U+1F600 = F0 9F 98 80 -> paire de substitution */
    CHECK(Parse(L"mailto:a@b.fr?body=%F0%9F%98%80", m));
    CHECK(m.body.size() == 2 && m.body[0] == 0xD83D && m.body[1] == 0xDE00);

    /* UTF-8 invalide ou tronqué, % mal formé */
    CHECK(Parse(L"mailto:a@b.fr?body=%FF%C3", m) && m.body == L"\uFFFD\uFFFD");
    CHECK(Parse(L"mailto:a@b.fr?body=100%&subject=%zz", m));
    CHECK(m.body == L"100%" && m.subject == L"%zz");
}

/** Sauts de ligne : normalisés dans le corps, supprimés des champs d'en-tête (pas d'injection). */
static void test_newlines()
{
    Message m;
    CHECK(Parse(L"mailto:a@b.fr?body=L1%0D%0AL2%0AL3%0DL4", m));
    CHECK(m.body == L"L1\nL2\nL3\nL4");
    CHECK(Parse(L"mailto:a@b.fr?subject=Objet%0D%0ABcc:%20x@y.fr&cc=c@d.fr%0D%0Abcc:z@z.fr", m));
    CHECK(m.subject == L"Objet Bcc: x@y.fr");
    CHECK(m.cc == L"c@d.fr bcc:z@z.fr");
    CHECK(m.bcc.empty());
}

/** Recherche de l'URL dans une ligne de commande. */
static void test_command_line()
{
    CHECK(FindUrlInCommandLine(L"\"C:\\Program Files\\Evolution\\EvolutionWin.exe\" mailto:a@b.fr") == L"mailto:a@b.fr");
    CHECK(FindUrlInCommandLine(L"EvolutionWin.exe \"mailto:a@b.fr?subject=a b\"") == L"mailto:a@b.fr?subject=a b");
    CHECK(FindUrlInCommandLine(L"EvolutionWin.exe --autre MAILTO:x@y.fr") == L"MAILTO:x@y.fr");
    CHECK(FindUrlInCommandLine(L"EvolutionWin.exe").empty());
    CHECK(FindUrlInCommandLine(L"").empty());
    CHECK(FindUrlInCommandLine(L"\"C:\\mailto:dossier\\EvolutionWin.exe\"").empty());
}

/** Valeur de registre cherchée par clé, nom et donnée. */
static bool Has(std::vector<RegistryValue> const& values, std::wstring const& key, std::wstring const& name, std::wstring const& data)
{
    for (auto const& v : values)
        if (v.key == key && v.value_name == name && v.data == data)
            return true;
    return false;
}

/** Enregistrement : clés attendues par Windows pour une application de messagerie. */
static void test_registration()
{
    std::wstring exe = L"C:\\Program Files\\Evolution\\EvolutionWin.exe";
    auto values = BuildRegistration(exe, L"Evolution", L"Messagerie");
    std::wstring client = L"Software\\Clients\\Mail\\EvolutionWin";

    CHECK(Has(values, client + L"\\Capabilities", L"ApplicationName", L"Evolution"));
    CHECK(Has(values, client + L"\\Capabilities\\URLAssociations", L"mailto", L"EvolutionWin.Url.mailto"));
    CHECK(Has(values, L"Software\\RegisteredApplications", L"EvolutionWin", client + L"\\Capabilities"));
    CHECK(Has(values, client + L"\\shell\\open\\command", L"", L"\"" + exe + L"\""));
    CHECK(Has(values, L"Software\\Classes\\EvolutionWin.Url.mailto", L"URL Protocol", L""));
    CHECK(Has(values, L"Software\\Classes\\EvolutionWin.Url.mailto\\shell\\open\\command", L"", L"\"" + exe + L"\" \"%1\""));

    auto keys = RegistrationKeysToDelete();
    CHECK(keys.size() == 2 && keys[0] == client && keys[1] == L"Software\\Classes\\EvolutionWin.Url.mailto");
}

/** Comparaison de la commande enregistrée avec l'exécutable courant. */
static void test_command_matches()
{
    std::wstring exe = L"C:\\Program Files\\Evolution\\EvolutionWin.exe";
    CHECK(CommandMatchesExe(L"\"" + exe + L"\" \"%1\"", exe));
    CHECK(CommandMatchesExe(L"\"c:\\program files\\evolution\\evolutionwin.exe\"", exe));
    CHECK(CommandMatchesExe(exe, exe));
    CHECK(!CommandMatchesExe(L"\"D:\\Autre\\EvolutionWin.exe\"", exe));
    CHECK(!CommandMatchesExe(L"\"" + exe + L"2\"", exe));
    CHECK(!CommandMatchesExe(L"", exe));
}

int main()
{
    test_simple();
    test_fields();
    test_decoding();
    test_newlines();
    test_command_line();
    test_registration();
    test_command_matches();
    std::printf("test_mailto : %d verifications, %d echec(s)\n", g_tests, g_failures);
    return g_failures == 0 ? 0 : 1;
}
