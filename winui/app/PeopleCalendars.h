#pragma once

/* Calendriers de personnes et d'équipes (annuaire Microsoft 365) : liste mémorisée
 * dans les réglages (clé « calendar.people ») et fusion avec les calendriers EDS.
 * Ces calendriers sont lus en direct par Microsoft Graph (lecture seule) ; ils
 * n'existent pas dans le registre EDS, d'où des sources « virtuelles » dont l'uid
 * commence par « person: ». */

#include "Settings.h"

#include <winrt/Windows.Data.Json.h>
#include <string>
#include <string_view>

namespace EvolutionWin::People
{
    /** @brief Préfixe des identifiants de calendriers de personnes (« person:adresse »). */
    inline constexpr std::wstring_view kUidPrefix = L"person:";

    /** @brief Nom du groupe sous lequel les calendriers de personnes sont listés. */
    inline constexpr wchar_t const* kGroupName = L"Personnes et équipes (annuaire)";

    /**
     * @brief Indique si un identifiant de calendrier désigne une personne de l'annuaire.
     * @param uid Identifiant de calendrier.
     * @return true pour « person:adresse ».
     */
    inline bool IsPersonUid(std::wstring_view uid)
    {
        return uid.size() > kUidPrefix.size() && uid.substr(0, kUidPrefix.size()) == kUidPrefix;
    }

    /**
     * @brief Identifiant de calendrier d'une personne.
     * @param email Adresse de la personne.
     * @return « person:adresse ».
     */
    inline winrt::hstring UidFor(winrt::hstring const& email)
    {
        return winrt::hstring{ std::wstring{ kUidPrefix } + std::wstring{ email } };
    }

    /**
     * @brief Adresse contenue dans un identifiant « person:adresse ».
     * @param uid Identifiant de calendrier de personne.
     * @return Adresse (vide si l'identifiant n'est pas celui d'une personne).
     */
    inline winrt::hstring EmailFromUid(winrt::hstring const& uid)
    {
        return IsPersonUid(std::wstring_view{ uid })
            ? winrt::hstring{ std::wstring_view{ uid }.substr(kUidPrefix.size()) } : winrt::hstring{};
    }

    /**
     * @brief Couleur attribuée au n-ième calendrier de personne (palette cyclique).
     * @param index Rang de la personne dans la liste.
     * @return Couleur « #rrggbb ».
     */
    inline winrt::hstring ColorFor(uint32_t index)
    {
        static constexpr wchar_t const* palette[] = {
            L"#3498db", L"#e67e22", L"#9b59b6", L"#16a085", L"#c0392b", L"#7f8c8d", L"#2ecc71", L"#d35400" };
        return palette[index % (sizeof palette / sizeof palette[0])];
    }

    /**
     * @brief Personnes mémorisées dans les réglages.
     * @return Tableau d'objets {"email", "name", "color", "account_uid"}.
     */
    inline winrt::Windows::Data::Json::JsonArray Load()
    {
        return ::EvolutionWin::Settings::Instance().GetArray(L"calendar.people");
    }

    /**
     * @brief Mémorise la liste des personnes dans les réglages.
     * @param people Tableau d'objets {"email", "name", "color", "account_uid"}.
     */
    inline void Save(winrt::Windows::Data::Json::JsonArray const& people)
    {
        ::EvolutionWin::Settings::Instance().Set(L"calendar.people", people);
    }

    /**
     * @brief Ajoute aux calendriers EDS les sources virtuelles des personnes mémorisées.
     * @param real Sources renvoyées par evo_core_list_sources (calendriers EDS).
     * @return Nouveau tableau : sources EDS puis une source par personne (backend « person »).
     */
    inline winrt::Windows::Data::Json::JsonArray MergeIntoSources(winrt::Windows::Data::Json::JsonArray const& real)
    {
        using namespace winrt::Windows::Data::Json;

        JsonArray merged;
        for (auto const& value : real)
            merged.Append(value);

        /* nom du compte (groupe de la liste des calendriers) d'après son identifiant */
        auto account_name = [&real](winrt::hstring const& account_uid)
        {
            for (auto const& value : real)
            {
                auto source = value.GetObject();
                if (!account_uid.empty() && source.GetNamedString(L"account_uid", L"") == account_uid)
                    return source.GetNamedString(L"account", L"");
            }
            return winrt::hstring{ kGroupName };
        };

        for (auto const& value : Load())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto person = value.GetObject();
            winrt::hstring email = person.GetNamedString(L"email", L"");
            if (email.empty())
                continue;

            JsonObject source;
            source.SetNamedValue(L"uid", JsonValue::CreateStringValue(UidFor(email)));
            source.SetNamedValue(L"name", JsonValue::CreateStringValue(person.GetNamedString(L"name", email)));
            source.SetNamedValue(L"kind", JsonValue::CreateStringValue(L"calendar"));
            /* le calendrier d'une personne reste dans la partie de son compte (ex. EVAM) */
            source.SetNamedValue(L"account", JsonValue::CreateStringValue(account_name(person.GetNamedString(L"account_uid", L""))));
            source.SetNamedValue(L"account_uid", JsonValue::CreateStringValue(person.GetNamedString(L"account_uid", L"")));
            source.SetNamedValue(L"account_email", JsonValue::CreateStringValue(email));
            source.SetNamedValue(L"color", JsonValue::CreateStringValue(person.GetNamedString(L"color", L"")));
            source.SetNamedValue(L"backend", JsonValue::CreateStringValue(L"person"));
            source.SetNamedValue(L"group_name", JsonValue::CreateStringValue(L""));
            source.SetNamedValue(L"default", JsonValue::CreateBooleanValue(false));
            merged.Append(source);
        }

        return merged;
    }
}
