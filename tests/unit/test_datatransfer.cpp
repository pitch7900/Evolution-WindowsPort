/**
 * @file test_datatransfer.cpp
 * @brief Tests unitaires de DataTransfer.h : détection des dossiers de courrier d'un profil
 * Thunderbird, fichiers Outlook, normalisation du dossier de destination.
 */

#include "../include/test_framework.h"
#include "../../winui/app/DataTransfer.h"

#include <fstream>

namespace fs = std::filesystem;
using namespace EvolutionWin::DataTransfer;

/** Crée un fichier vide (et ses dossiers parents). */
static void Touch(fs::path const& file)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file).put('x');
}

static void test_outlook_files()
{
    TEST_CASE("fichiers Outlook: .pst et .ost reconnus, casse ignoree");
    {
        ASSERT_TRUE(IsOutlookDataFile(L"C:\\Users\\x\\Documents\\Outlook\\archive.PST"));
        ASSERT_TRUE(IsOutlookDataFile(L"boite.ost"));
        ASSERT_FALSE(IsOutlookDataFile(L"courrier.mbox"));
        ASSERT_FALSE(IsOutlookDataFile(L"pst"));
    }
    TEST_CASE_END();
}

static void test_thunderbird_detection()
{
    TEST_CASE("Thunderbird: dossiers Mail et ImapMail de chaque profil");
    {
        fs::path root = fs::temp_directory_path() / L"evo-test-thunderbird";
        fs::remove_all(root);
        Touch(root / L"abcd1234.default-release" / L"Mail" / L"Local Folders" / L"Inbox");
        Touch(root / L"abcd1234.default-release" / L"ImapMail" / L"imap.free.fr" / L"INBOX");
        fs::create_directories(root / L"abcd1234.default-release" / L"ImapMail" / L"vide.example.org");
        Touch(root / L"zz99.autre" / L"Mail" / L"pop.example.org" / L"Inbox");
        Touch(root / L"fichier-isole.txt");

        auto dirs = FindThunderbirdMailDirs(root);
        ASSERT_EQ_INT(3, static_cast<int>(dirs.size()));
        /* triés par libellé : autre — pop, default-release — Local Folders, default-release — imap */
        ASSERT_TRUE(dirs[0].label.find(L"autre") == 0);
        ASSERT_TRUE(dirs[1].label.find(L"default-release") == 0);
        ASSERT_TRUE(dirs[1].label.find(L"Local Folders") != std::wstring::npos);
        ASSERT_TRUE(dirs[2].label.find(L"imap.free.fr") != std::wstring::npos);
        ASSERT_TRUE(fs::is_directory(dirs[0].path));

        /* Thunderbird absent */
        ASSERT_EQ_INT(0, static_cast<int>(FindThunderbirdMailDirs(root / L"absent").size()));
        fs::remove_all(root);
    }
    TEST_CASE_END();
}

static void test_names_and_dates()
{
    TEST_CASE("noms de profil, dossier de destination et fin de journee");
    {
        ASSERT_TRUE(ProfileDisplayName(L"x1y2.default") == L"default");
        ASSERT_TRUE(ProfileDisplayName(L"sansPoint") == L"sansPoint");
        ASSERT_TRUE(NormalizeFolderPath(L"  Importé / Thunderbird \\ 2024/ ") == L"Importé/Thunderbird/2024");
        ASSERT_TRUE(NormalizeFolderPath(L"A//B") == L"A/B");
        ASSERT_TRUE(NormalizeFolderPath(L" / ").empty());
        ASSERT_EQ_INT(86399, static_cast<int>(EndOfDay(0)));
    }
    TEST_CASE_END();
}

/**
 * @brief Point d'entrée de la suite de tests des assistants d'export / import (côté interface).
 * @return 0 si succès, 1 en cas d'échec.
 */
int main()
{
    TEST_SUITE_BEGIN("Assistants d'export / import (DataTransfer.h)");

    test_outlook_files();
    test_thunderbird_detection();
    test_names_and_dates();

    return test_suite_summary();
}
