#pragma once

/* Calendriers de personnes et d'équipes (annuaire Microsoft 365) : liste mémorisée
 * dans les réglages (clé « calendar.people ») et fusion avec les calendriers EDS.
 * Ces calendriers sont lus en direct par Microsoft Graph (lecture seule) ; ils
 * n'existent pas dans le registre EDS, d'où des sources « virtuelles » dont l'uid
 * commence par « person: ». */

#include "MainWindow.Helpers.h"
#include "Settings.h"

#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::People
{
    /** @brief Préfixe des identifiants de calendriers de personnes (« person:adresse »). */
    inline constexpr std::wstring_view kUidPrefix = L"person:";

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
     * @brief Calendriers importés des groupes d'Outlook (lus par EWS), mémorisés dans les réglages.
     * @return Tableau d'objets {"email", "name", "group", "color", "account_uid"}.
     */
    inline winrt::Windows::Data::Json::JsonArray LoadNav()
    {
        return ::EvolutionWin::Settings::Instance().GetArray(L"calendar.nav");
    }

    /**
     * @brief Mémorise les calendriers importés des groupes d'Outlook.
     * @param people Tableau d'objets {"email", "name", "group", "color", "account_uid"}.
     */
    inline void SaveNav(winrt::Windows::Data::Json::JsonArray const& people)
    {
        ::EvolutionWin::Settings::Instance().Set(L"calendar.nav", people);
    }

    /**
     * @brief Toutes les personnes : celles ajoutées à la main, puis celles importées d'Outlook (sans doublon d'adresse).
     * @return Tableau d'objets {"email", "name", "group", "color", "account_uid"}.
     */
    inline winrt::Windows::Data::Json::JsonArray LoadAll()
    {
        using namespace winrt::Windows::Data::Json;

        JsonArray all;
        std::vector<std::wstring> seen;
        for (auto const& list : { ::EvolutionWin::Settings::Instance().GetArray(L"calendar.people"), LoadNav() })
        {
            for (auto const& value : list)
            {
                if (value.ValueType() != JsonValueType::Object)
                    continue;
                std::wstring email{ value.GetObject().GetNamedString(L"email", L"") };
                if (email.empty())
                    continue;
                /* une personne peut figurer dans plusieurs groupes : dédoublonnage sur adresse + groupe */
                std::wstring key = email + L"|" + std::wstring{ value.GetObject().GetNamedString(L"group", L"") };
                if (std::find(seen.begin(), seen.end(), key) != seen.end())
                    continue;
                seen.push_back(key);
                all.Append(value);
            }
        }
        return all;
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
     * @brief Nom affiché d'un groupe de calendriers Outlook (traduit pour les groupes standard).
     * @param group Objet {"name", "class_id", "calendars"} renvoyé par evo_core_list_calendar_groups.
     * @return Nom à afficher ; vide pour le groupe « Mes calendriers » (les calendriers du compte, sans sous-groupe).
     */
    inline winrt::hstring GroupDisplayName(winrt::Windows::Data::Json::JsonObject const& group)
    {
        std::wstring class_id{ group.GetNamedString(L"class_id", L"") };
        if (class_id.rfind(L"0006f0b7", 0) == 0)
            return winrt::hstring{};
        if (class_id.rfind(L"0006f0b8", 0) == 0)
            return ::EvolutionWin::Helpers::Tr(L"Autres calendriers");
        if (class_id.rfind(L"0006f0b9", 0) == 0)
            return ::EvolutionWin::Helpers::Tr(L"Calendriers des contacts");
        return group.GetNamedString(L"name", L"");
    }

    /**
     * @brief Ajoute aux calendriers EDS les groupes Outlook et les sources virtuelles des personnes.
     *
     * Les calendriers Exchange Online sont classés dans le groupe Outlook qui les contient ; chaque
     * groupe Outlook est affiché, même vide (Graph n'expose pas les membres des groupes d'équipes ou de salles).
     * @param real Sources renvoyées par evo_core_list_sources (calendriers EDS).
     * @param groups Groupes renvoyés par evo_core_list_calendar_groups (nullptr : aucun).
     * @return Nouveau tableau : sources EDS, repères de groupes vides (« placeholder »), une source par personne.
     */
    inline winrt::Windows::Data::Json::JsonArray MergeIntoSources(winrt::Windows::Data::Json::JsonArray const& real,
        winrt::Windows::Data::Json::JsonArray const& groups = nullptr)
    {
        using namespace winrt::Windows::Data::Json;

        JsonArray merged;
        winrt::hstring m365_account, m365_account_uid;

        for (auto const& value : real)
        {
            auto source = value.GetObject();
            bool m365 = source.GetNamedString(L"backend", L"") == L"microsoft365";
            if (m365 && m365_account_uid.empty())
            {
                m365_account = source.GetNamedString(L"account", L"");
                m365_account_uid = source.GetNamedString(L"account_uid", L"");
            }

            winrt::hstring group_name;
            if (m365 && groups)
            {
                winrt::hstring name = source.GetNamedString(L"name", L"");
                for (auto const& group_value : groups)
                {
                    auto group = group_value.GetObject();
                    for (auto const& calendar : group.GetNamedArray(L"calendars", JsonArray{}))
                    {
                        if (calendar.GetObject().GetNamedString(L"name", L"") == name)
                            group_name = GroupDisplayName(group);
                    }
                }
            }

            if (group_name.empty() || !source.GetNamedString(L"group_name", L"").empty())
            {
                merged.Append(value);
                continue;
            }
            JsonObject copy = JsonObject::Parse(source.Stringify());
            copy.SetNamedValue(L"group_name", JsonValue::CreateStringValue(group_name));
            merged.Append(copy);
        }

        /* groupes Outlook (même vides) : repères affichés comme sous-groupes du compte */
        /* seuls les groupes qui contiennent au moins un calendrier de l'annuaire ou d'Exchange restent affichés ;
         * un groupe vide (« Autres calendriers », « Tous les calendriers de groupe »…) n'apporte rien */
        auto group_has_content = [&](winrt::hstring const& display)
        {
            for (auto const& value : merged)
            {
                auto item = value.GetObject();
                if (item.GetNamedString(L"group_name", L"") == display && !item.GetNamedBoolean(L"placeholder", false))
                    return true;
            }
            for (auto const& value : LoadAll())
                if (value.ValueType() == JsonValueType::Object && value.GetObject().GetNamedString(L"group", L"") == display)
                    return true;
            return false;
        };
        if (groups && !m365_account_uid.empty())
        {
            for (auto const& group_value : groups)
            {
                winrt::hstring display = GroupDisplayName(group_value.GetObject());
                if (display.empty() || !group_has_content(display))
                    continue;
                JsonObject marker;
                marker.SetNamedValue(L"uid", JsonValue::CreateStringValue(L"group:" + m365_account_uid + L":" + display));
                marker.SetNamedValue(L"name", JsonValue::CreateStringValue(L""));
                marker.SetNamedValue(L"kind", JsonValue::CreateStringValue(L"calendar"));
                marker.SetNamedValue(L"account", JsonValue::CreateStringValue(m365_account));
                marker.SetNamedValue(L"account_uid", JsonValue::CreateStringValue(m365_account_uid));
                marker.SetNamedValue(L"backend", JsonValue::CreateStringValue(L"group"));
                marker.SetNamedValue(L"group_name", JsonValue::CreateStringValue(display));
                marker.SetNamedValue(L"placeholder", JsonValue::CreateBooleanValue(true));
                marker.SetNamedValue(L"default", JsonValue::CreateBooleanValue(false));
                merged.Append(marker);
            }
        }

        /* nom du compte (groupe de la liste des calendriers) d'après son identifiant */
        auto account_name = [&real](winrt::hstring const& account_uid)
        {
            for (auto const& value : real)
            {
                auto source = value.GetObject();
                if (!account_uid.empty() && source.GetNamedString(L"account_uid", L"") == account_uid)
                    return source.GetNamedString(L"account", L"");
            }
            return ::EvolutionWin::Helpers::Tr(L"Personnes et équipes (annuaire)");
        };

        /* calendriers déjà fournis par EDS (même nom, même compte) : non répétés */
        auto is_real = [&real](winrt::hstring const& account_uid, winrt::hstring const& name)
        {
            for (auto const& value : real)
            {
                auto source = value.GetObject();
                if (source.GetNamedString(L"account_uid", L"") == account_uid && source.GetNamedString(L"name", L"") == name)
                    return true;
            }
            return false;
        };

        for (auto const& value : LoadAll())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto person = value.GetObject();
            winrt::hstring email = person.GetNamedString(L"email", L"");
            if (email.empty() || is_real(person.GetNamedString(L"account_uid", L""), person.GetNamedString(L"name", L"")))
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
            /* groupe de calendriers (ex. « Équipe : DUPONT Marie », « Team Infra N2 ») */
            source.SetNamedValue(L"group_name", JsonValue::CreateStringValue(person.GetNamedString(L"group", L"")));
            source.SetNamedValue(L"default", JsonValue::CreateBooleanValue(false));
            merged.Append(source);
        }

        return merged;
    }
}
