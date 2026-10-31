#pragma once

/* Préférences de l'interface (tailles des panneaux, choix d'affichage,
 * listes de confiance…) : objet JSON dans
 * %LOCALAPPDATA%\EvolutionWin\settings.json (application non empaquetée,
 * sans ApplicationData). Lu au premier accès, réécrit à chaque Set().
 * À utiliser depuis le thread de l'interface. Clés en minuscules avec
 * tirets, regroupées par préfixe (ex. "layout.folders-width"). */

#include <winrt/Windows.Data.Json.h>
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

        double GetNumber(winrt::hstring const& key, double fallback)
        {
            auto value = Lookup(key);
            return value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Number ? value.GetNumber() : fallback;
        }

        bool GetBoolean(winrt::hstring const& key, bool fallback)
        {
            auto value = Lookup(key);
            return value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Boolean ? value.GetBoolean() : fallback;
        }

        winrt::hstring GetString(winrt::hstring const& key, winrt::hstring const& fallback)
        {
            auto value = Lookup(key);
            return value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String ? value.GetString() : fallback;
        }

        /* tableau JSON (ex. liste d'expéditeurs de confiance) ; vide si absent */
        winrt::Windows::Data::Json::JsonArray GetArray(winrt::hstring const& key)
        {
            auto value = Lookup(key);
            if (value && value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Array)
                return value.GetArray();
            return winrt::Windows::Data::Json::JsonArray{};
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

        void Set(winrt::hstring const& key, winrt::Windows::Data::Json::IJsonValue const& value)
        {
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
