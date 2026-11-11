#pragma once

/* Préférences de l'interface (tailles des panneaux, choix d'affichage,
 * listes de confiance…) : objet JSON dans
 * %LOCALAPPDATA%\EvolutionWin\settings.json (application non empaquetée,
 * sans ApplicationData). Lu au premier accès, réécrit à chaque Set().
 * À utiliser depuis le thread de l'interface. Clés en minuscules avec
 * tirets, regroupées par préfixe (ex. "layout.folders-width"). */

#include <winrt/Windows.Data.Json.h>
#include "Policy.h"
#include <fstream>
#include <sstream>
#include <string>

namespace EvolutionWin
{
    class Settings
    {
    public:
        static Settings& Instance()
        {
            static Settings settings;
            return settings;
        }

        /**
         * @brief Recupere une valeur numerique depuis les parametres.
         * @param key Cle du parametre recherche.
         * @param fallback Valeur par defaut si le parametre n'existe pas.
         * @return Valeur numerique lue ou valeur par defaut.
         */
        double GetNumber(winrt::hstring const& key, double fallback)
        {
            if (auto const* forced = Policy::ForcedSetting(std::wstring{ key }))
            {
                if (forced->is_number)
                    return static_cast<double>(forced->number);
                try { return std::stod(forced->text); } catch (...) {}
            }
            auto value = Lookup(key);
            if (!value)
                return fallback;
            if (value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
                return value.GetNumber();
            if (value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String)
            {
                try
                {
                    return std::stod(std::wstring(value.GetString()));
                }
                catch (...) {}
            }
            return fallback;
        }

        /**
         * @brief Recupere un entier depuis les parametres.
         * @param key Cle du parametre recherche.
         * @param fallback Valeur par defaut si le parametre n'existe pas.
         * @return Entier lu ou valeur par defaut.
         */
        int GetInt(winrt::hstring const& key, int fallback)
        {
            return static_cast<int>(GetNumber(key, fallback));
        }

        bool GetBoolean(winrt::hstring const& key, bool fallback)
        {
            if (auto const* forced = Policy::ForcedSetting(std::wstring{ key }))
                return Policy::AsBool(*forced);
            auto value = Lookup(key);
            return value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Boolean ? value.GetBoolean() : fallback;
        }

        /**
         * @brief Recupere une chaine depuis les parametres.
         * @param key Cle du parametre recherche.
         * @param fallback Valeur par defaut si le parametre n'existe pas.
         * @return Chaine lue ou valeur par defaut.
         */
        winrt::hstring GetString(winrt::hstring const& key, winrt::hstring const& fallback)
        {
            if (auto const* forced = Policy::ForcedSetting(std::wstring{ key }))
                return forced->is_number ? winrt::to_hstring(static_cast<int64_t>(forced->number)) : winrt::hstring{ forced->text };
            auto value = Lookup(key);
            if (!value)
                return fallback;
            if (value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String)
                return value.GetString();
            if (value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number)
            {
                double num = value.GetNumber();
                if (num == static_cast<int64_t>(num))
                    return winrt::to_hstring(static_cast<int64_t>(num));
                return winrt::to_hstring(num);
            }
            return fallback;
        }

        /* tableau JSON (ex. liste d'expéditeurs de confiance) ; vide si absent */
        winrt::Windows::Data::Json::JsonArray GetArray(winrt::hstring const& key)
        {
            auto value = Lookup(key);
            if (value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Array)
                return value.GetArray();
            return winrt::Windows::Data::Json::JsonArray{};
        }

        double GetDouble(winrt::hstring const& key, double fallback)
        {
            return GetNumber(key, fallback);
        }

        void SetDouble(winrt::hstring const& key, double value)
        {
            SetNumber(key, value);
        }

        void SetNumber(winrt::hstring const& key, double value)
        {
            Set(key, winrt::Windows::Data::Json::JsonValue::CreateNumberValue(value));
        }

        void SetBoolean(winrt::hstring const& key, bool value)
        {
            Set(key, winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(value));
        }

        void SetString(winrt::hstring const& key, winrt::hstring const& value)
        {
            Set(key, winrt::Windows::Data::Json::JsonValue::CreateStringValue(value));
        }

        /**
         * @brief Indique si un parametre est impose par une strategie de groupe (Policy.h).
         * @param key Cle du parametre.
         * @return true si la valeur est verrouillee (lecture seule dans l'interface).
         */
        bool IsLocked(winrt::hstring const& key)
        {
            return Policy::IsLocked(std::wstring{ key });
        }

        void Set(winrt::hstring const& key, winrt::Windows::Data::Json::IJsonValue const& value)
        {
            if (Policy::IsLocked(std::wstring{ key }))
                return; /* parametre impose par strategie de groupe : modification ignoree */
            Load();
            m_root.SetNamedValue(key, value);
            Save();
        }

    private:
        Settings() = default;

        winrt::Windows::Data::Json::IJsonValue Lookup(winrt::hstring const& key)
        {
            Load();
            return m_root.HasKey(key) ? m_root.GetNamedValue(key) : nullptr;
        }

        static std::wstring FilePath()
        {
            wchar_t local[MAX_PATH]{};
            GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
            std::wstring dir = std::wstring{ local } + L"\\EvolutionWin";
            CreateDirectoryW(dir.c_str(), nullptr);
            return dir + L"\\settings.json";
        }

        void Load()
        {
            if (m_loaded)
                return;
            m_loaded = true;

            std::ifstream file(FilePath(), std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            winrt::Windows::Data::Json::JsonObject parsed;
            if (winrt::Windows::Data::Json::JsonObject::TryParse(winrt::to_hstring(content.str()), parsed))
                m_root = parsed;
        }

        void Save()
        {
            std::ofstream file(FilePath(), std::ios::binary | std::ios::trunc);
            file << winrt::to_string(m_root.Stringify());
        }

        bool m_loaded{ false };
        winrt::Windows::Data::Json::JsonObject m_root;
    };
}
