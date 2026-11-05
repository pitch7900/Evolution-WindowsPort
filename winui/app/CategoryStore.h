#pragma once

/**
 * @file CategoryStore.h
 * @brief Registre unique des catégories de l'application, persisté dans settings.json.
 *
 * Fait le lien entre la logique pure (Categories.h) et les préférences
 * (Settings.h, clé "categories.registry"). À utiliser depuis le thread de
 * l'interface, comme Settings.
 */

#include "Categories.h"
#include "Settings.h"
#include <winrt/Windows.UI.h>

namespace EvolutionWin
{
    /**
     * @brief Accès singleton au registre des catégories (nom -> couleur).
     */
    class CategoryStore
    {
    public:
        /**
         * @brief Renvoie le registre partagé, chargé depuis les réglages au premier accès.
         * @return Référence modifiable ; appeler Save() après toute modification.
         */
        static Categories::CategoryRegistry& Registry()
        {
            auto& state = State();
            if (!state.loaded)
            {
                state.loaded = true;
                std::wstring json{ Settings::Instance().GetString(L"categories.registry", L"") };
                state.registry = json.empty() ? Categories::CategoryRegistry{} : Categories::CategoryRegistry::Deserialize(json);
            }
            return state.registry;
        }

        /** @brief Enregistre le registre courant dans settings.json. */
        static void Save()
        {
            Settings::Instance().SetString(L"categories.registry", winrt::hstring{ Registry().Serialize() });
        }

        /**
         * @brief Couleur d'une catégorie sous forme de couleur WinUI (repli stable si inconnue).
         * @param name Nom de la catégorie.
         * @return Couleur opaque.
         */
        static winrt::Windows::UI::Color ColorOf(std::wstring_view name)
        {
            uint8_t r = 0, g = 0, b = 0;
            Registry().ColorOf(name, r, g, b);
            return winrt::Windows::UI::Color{ 255, r, g, b };
        }

        /**
         * @brief Lit un tableau JSON de noms de catégories (champ "categories" d'un élément du moteur).
         * @param array Tableau JSON (peut être vide).
         * @return Noms non vides, dans l'ordre.
         */
        static std::vector<std::wstring> NamesFromJson(winrt::Windows::Data::Json::JsonArray const& array)
        {
            std::vector<std::wstring> names;
            for (auto const& value : array)
            {
                if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
                    continue;
                std::wstring name = Categories::TrimName(std::wstring_view{ value.GetString() });
                if (!name.empty())
                    names.push_back(std::move(name));
            }
            return names;
        }

        /**
         * @brief Catégories d'un objet JSON du moteur (membre "categories" ou, pour les courriels, "labels").
         * @param item Objet JSON de l'élément (message, contact, tâche, événement).
         * @return Noms de catégories de l'élément.
         */
        static std::vector<std::wstring> CategoriesOf(winrt::Windows::Data::Json::JsonObject const& item)
        {
            for (auto const* key : { L"categories", L"labels" })
            {
                if (item.HasKey(key) && item.GetNamedValue(key).ValueType() == winrt::Windows::Data::Json::JsonValueType::Array)
                    return NamesFromJson(item.GetNamedArray(key));
            }
            return {};
        }

        /**
         * @brief Couleur d'affichage d'un événement de calendrier : celle de sa première catégorie,
         *        à défaut celle de son agenda.
         * @param event Événement au format JSON du moteur.
         * @return Couleur « #RRGGBB » (celle de l'agenda, éventuellement vide, sans catégorie).
         */
        static winrt::hstring EventColorHex(winrt::Windows::Data::Json::JsonObject const& event)
        {
            auto names = CategoriesOf(event);
            if (!names.empty())
            {
                uint8_t r = 0, g = 0, b = 0;
                Registry().ColorOf(names.front(), r, g, b);
                return winrt::hstring{ Categories::FormatHexColor(r, g, b) };
            }
            return event.GetNamedString(L"calendar_color", L"");
        }

        /**
         * @brief Sérialise des noms de catégories en tableau JSON pour le moteur.
         * @param names Noms de catégories.
         * @return JsonArray de chaînes.
         */
        static winrt::Windows::Data::Json::JsonArray ToJsonArray(std::vector<std::wstring> const& names)
        {
            winrt::Windows::Data::Json::JsonArray array;
            for (auto const& name : names)
                array.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(name));
            return array;
        }

    private:
        struct StateData
        {
            bool loaded{ false };
            Categories::CategoryRegistry registry;
        };

        static StateData& State()
        {
            static StateData state;
            return state;
        }
    };
}
