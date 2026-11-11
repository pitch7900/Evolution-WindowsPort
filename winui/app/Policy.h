#pragma once

/**
 * @file Policy.h
 * @brief Strategies de groupe (GPO) et cles de registre de configuration imposee.
 *
 * Les strategies sont lues dans la base de registre, par ordre de priorite :
 *   1. HKLM\SOFTWARE\Policies\EvolutionWin   (machine : modele ADMX, GPO, Intune)
 *   2. HKCU\Software\Policies\EvolutionWin   (utilisateur : repli)
 *
 * Valeurs generales (sous la cle racine) :
 *   - DisableAutoUpdate (DWORD) : 1 = plus aucune verification ni installation de mise a jour.
 *
 * Parametres imposes (sous-cle « Settings ») : un nom de valeur = une cle de
 * Settings (ex. « layout.reading-pane-position »), REG_DWORD (booleen ou nombre)
 * ou REG_SZ (chaine). La valeur imposee l'emporte sur le fichier settings.json
 * et le parametre n'est plus modifiable dans l'interface.
 *
 * Aucune dependance a WinRT : en-tete testable seul (tests/unit/test_policy.cpp).
 */

#include <windows.h>
#include <cstring>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <vector>

namespace EvolutionWin::Policy
{
    /** @brief Valeur de strategie lue dans le registre. */
    struct Value
    {
        bool is_number{ false };   /**< true : REG_DWORD / REG_QWORD ; false : REG_SZ */
        unsigned long long number{ 0 };
        std::wstring text;
    };

    /** @brief Emplacement de registre d'une source de strategies. */
    struct Source
    {
        HKEY hive;
        std::wstring path;
    };

    /**
     * @brief Sources de strategies, de la plus prioritaire a la moins prioritaire.
     * @return Liste modifiable (les tests y placent des cles factices sous HKCU).
     */
    inline std::vector<Source>& Sources()
    {
        static std::vector<Source> sources{
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\EvolutionWin" },
            { HKEY_CURRENT_USER, L"Software\\Policies\\EvolutionWin" },
        };
        return sources;
    }

    /**
     * @brief Lit une valeur du registre (REG_DWORD, REG_QWORD, REG_SZ ou REG_EXPAND_SZ).
     * @param hive Ruche racine.
     * @param path Chemin de la cle.
     * @param name Nom de la valeur.
     * @param out Resultat si la valeur existe et est d'un type pris en charge.
     * @return true si la valeur a ete lue.
     */
    inline bool ReadRegistryValue(HKEY hive, std::wstring const& path, std::wstring const& name, Value& out)
    {
        HKEY key{};
        if (RegOpenKeyExW(hive, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
            return false;
        DWORD type = 0, size = 0;
        bool ok = false;
        if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS)
        {
            std::vector<BYTE> data(size + sizeof(wchar_t) * 2, 0);
            DWORD read = size;
            if (RegQueryValueExW(key, name.c_str(), nullptr, &type, data.data(), &read) == ERROR_SUCCESS)
            {
                if (type == REG_DWORD && read >= sizeof(DWORD))
                {
                    DWORD v = 0;
                    memcpy(&v, data.data(), sizeof(v));
                    out.is_number = true;
                    out.number = v;
                    out.text.clear();
                    ok = true;
                }
                else if (type == REG_QWORD && read >= sizeof(ULONGLONG))
                {
                    ULONGLONG v = 0;
                    memcpy(&v, data.data(), sizeof(v));
                    out.is_number = true;
                    out.number = v;
                    out.text.clear();
                    ok = true;
                }
                else if (type == REG_SZ || type == REG_EXPAND_SZ)
                {
                    out.is_number = false;
                    out.number = 0;
                    out.text = reinterpret_cast<wchar_t*>(data.data());
                    ok = true;
                }
            }
        }
        RegCloseKey(key);
        return ok;
    }

    /**
     * @brief Lit une valeur de strategie dans la premiere source qui la definit.
     * @param subkey Sous-cle relative a la racine des strategies (vide : la racine).
     * @param name Nom de la valeur.
     * @param out Resultat.
     * @return true si une source definit la valeur.
     */
    inline bool Read(std::wstring const& subkey, std::wstring const& name, Value& out)
    {
        for (auto const& source : Sources())
        {
            std::wstring path = subkey.empty() ? source.path : source.path + L"\\" + subkey;
            if (ReadRegistryValue(source.hive, path, name, out))
                return true;
        }
        return false;
    }

    /**
     * @brief Interprete une valeur de strategie comme un booleen.
     * @param value Valeur lue.
     * @return true pour un nombre non nul ou une chaine « 1 », « true », « oui », « yes ».
     */
    inline bool AsBool(Value const& value)
    {
        if (value.is_number)
            return value.number != 0;
        std::wstring s;
        for (wchar_t c : value.text)
            s.push_back(static_cast<wchar_t>(std::towlower(c)));
        return s == L"1" || s == L"true" || s == L"oui" || s == L"yes";
    }

    /**
     * @brief Lit une strategie booleenne generale.
     * @param name Nom de la valeur sous la cle racine.
     * @param fallback Valeur si aucune strategie ne la definit.
     * @return Valeur imposee ou repli.
     */
    inline bool GetBool(std::wstring const& name, bool fallback)
    {
        Value v;
        return Read(L"", name, v) ? AsBool(v) : fallback;
    }

    /**
     * @brief Indique si les mises a jour automatiques sont interdites par strategie.
     * @return true si DisableAutoUpdate est actif.
     */
    inline bool AutoUpdateDisabled()
    {
        return GetBool(L"DisableAutoUpdate", false);
    }

    /** @brief Instantane des parametres imposes et indicateur de chargement. */
    struct ForcedTable
    {
        std::unordered_map<std::wstring, Value> values;
        bool loaded{ false };
    };

    /** @return Table unique des parametres imposes. */
    inline ForcedTable& Table()
    {
        static ForcedTable table;
        return table;
    }

    /**
     * @brief Parametres imposes (sous-cle « Settings »), charges a la premiere demande.
     * @return Table cle de Settings -> valeur imposee (la source la plus prioritaire l'emporte).
     */
    inline std::unordered_map<std::wstring, Value> const& ForcedSettings()
    {
        ForcedTable& table = Table();
        if (table.loaded)
            return table.values;
        table.loaded = true;
        for (auto const& source : Sources())
        {
            std::wstring path = source.path + L"\\Settings";
            HKEY key{};
            if (RegOpenKeyExW(source.hive, path.c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
                continue;
            for (DWORD i = 0;; ++i)
            {
                wchar_t name[512];
                DWORD len = 512;
                if (RegEnumValueW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                    break;
                if (table.values.count(name))
                    continue;
                Value v;
                if (ReadRegistryValue(source.hive, path, name, v))
                    table.values.emplace(name, std::move(v));
            }
            RegCloseKey(key);
        }
        return table.values;
    }

    /**
     * @brief Oublie l'instantane : il sera relu a la prochaine demande (tests).
     */
    inline void Reload()
    {
        Table().values.clear();
        Table().loaded = false;
    }

    /**
     * @brief Cherche le parametre impose pour une cle de Settings.
     * @param key Cle de Settings (ex. « layout.reading-pane-position »).
     * @return Pointeur vers la valeur imposee, nullptr si le parametre est libre.
     */
    inline Value const* ForcedSetting(std::wstring const& key)
    {
        auto const& values = ForcedSettings();
        auto it = values.find(key);
        return it == values.end() ? nullptr : &it->second;
    }

    /**
     * @brief Indique si un parametre est verrouille par strategie.
     * @param key Cle de Settings.
     * @return true si une strategie l'impose.
     */
    inline bool IsLocked(std::wstring const& key)
    {
        return ForcedSetting(key) != nullptr;
    }
}
