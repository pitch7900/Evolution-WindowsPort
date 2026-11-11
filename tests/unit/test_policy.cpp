/**
 * @file test_policy.cpp
 * @brief Tests unitaires du lecteur de strategies de groupe (Policy.h).
 *
 * Les « strategies » sont ecrites sous HKCU\Software\EvolutionWin-PolicyTest
 * (deux sources factices : machine et utilisateur) puis supprimees a la fin.
 */

#include "../include/test_framework.h"
#include "../../winui/app/Policy.h"

#include <string>

using namespace EvolutionWin::Policy;

static const wchar_t* MACHINE = L"Software\\EvolutionWin-PolicyTest\\Machine";
static const wchar_t* USER = L"Software\\EvolutionWin-PolicyTest\\User";

/**
 * @brief Ecrit une valeur REG_DWORD sous HKCU.
 * @param path Cle.
 * @param name Valeur.
 * @param v Donnee.
 */
static void put_dword(const wchar_t* path, const wchar_t* name, DWORD v)
{
    HKEY key{};
    RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(key);
}

/**
 * @brief Ecrit une valeur REG_SZ sous HKCU.
 * @param path Cle.
 * @param name Valeur.
 * @param v Donnee.
 */
static void put_string(const wchar_t* path, const wchar_t* name, const wchar_t* v)
{
    HKEY key{};
    RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v),
                   static_cast<DWORD>((wcslen(v) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

/** @brief Efface la branche de test et vide le cache du lecteur. */
static void reset_registry()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\EvolutionWin-PolicyTest");
    Reload();
}

static void test_general_values()
{
    TEST_CASE("Strategie generale : absente, DWORD, chaine, priorite machine")
    {
        reset_registry();
        Sources() = { { HKEY_CURRENT_USER, MACHINE }, { HKEY_CURRENT_USER, USER } };

        ASSERT_FALSE(AutoUpdateDisabled());                 /* aucune strategie : repli */
        ASSERT_TRUE(GetBool(L"DisableAutoUpdate", true));   /* repli personnalise */

        put_dword(USER, L"DisableAutoUpdate", 1);
        ASSERT_TRUE(AutoUpdateDisabled());                  /* source utilisateur en repli */

        put_dword(MACHINE, L"DisableAutoUpdate", 0);
        ASSERT_FALSE(AutoUpdateDisabled());                 /* la machine l'emporte sur l'utilisateur */

        put_string(MACHINE, L"DisableAutoUpdate", L"Oui");
        ASSERT_TRUE(AutoUpdateDisabled());                  /* chaine « oui » acceptee */

        put_string(MACHINE, L"DisableAutoUpdate", L"non");
        ASSERT_FALSE(AutoUpdateDisabled());
    }
    TEST_CASE_END();
}

static void test_forced_settings()
{
    TEST_CASE("Parametres imposes : types, verrou, priorite, absence")
    {
        reset_registry();
        Sources() = { { HKEY_CURRENT_USER, MACHINE }, { HKEY_CURRENT_USER, USER } };

        std::wstring machine_settings = std::wstring(MACHINE) + L"\\Settings";
        std::wstring user_settings = std::wstring(USER) + L"\\Settings";
        put_dword(machine_settings.c_str(), L"mail.mark-read-delay", 7);
        put_string(machine_settings.c_str(), L"layout.reading-pane-position", L"bottom");
        put_dword(user_settings.c_str(), L"mail.mark-read-delay", 99);   /* ecrase par la machine */
        put_dword(user_settings.c_str(), L"ui.compact", 1);              /* seulement cote utilisateur */
        Reload();

        ASSERT_TRUE(IsLocked(L"mail.mark-read-delay"));
        ASSERT_TRUE(IsLocked(L"layout.reading-pane-position"));
        ASSERT_TRUE(IsLocked(L"ui.compact"));
        ASSERT_FALSE(IsLocked(L"ui.libre"));
        ASSERT_TRUE(ForcedSetting(L"ui.libre") == nullptr);

        Value const* delay = ForcedSetting(L"mail.mark-read-delay");
        ASSERT_TRUE(delay != nullptr && delay->is_number);
        ASSERT_EQ_INT(7, static_cast<int>(delay->number));

        Value const* pane = ForcedSetting(L"layout.reading-pane-position");
        ASSERT_TRUE(pane != nullptr && !pane->is_number);
        ASSERT_TRUE(pane->text == L"bottom");

        ASSERT_TRUE(AsBool(*ForcedSetting(L"ui.compact")));
    }
    TEST_CASE_END();
}

static void test_missing_keys()
{
    TEST_CASE("Cles de strategie inexistantes : aucune erreur, aucun verrou")
    {
        reset_registry();
        Sources() = { { HKEY_CURRENT_USER, L"Software\\EvolutionWin-PolicyTest\\Inexistante" } };
        Reload();
        ASSERT_FALSE(AutoUpdateDisabled());
        ASSERT_FALSE(IsLocked(L"quelconque"));
        ASSERT_EQ_INT(0, static_cast<int>(ForcedSettings().size()));
    }
    TEST_CASE_END();
}

int main(void)
{
    TEST_SUITE_BEGIN("Strategies de groupe (Policy.h)");

    test_general_values();
    test_forced_settings();
    test_missing_keys();

    reset_registry();
    return test_suite_summary();
}
