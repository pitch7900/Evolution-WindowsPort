#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "PeopleCalendars.h"

#include <winrt/Windows.System.h>
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

/* Calendriers de personnes et d'équipes de l'annuaire Microsoft 365 : boîte de choix
 * (mon équipe, recherche dans l'annuaire, personnes déjà ajoutées) et lecture des
 * événements en direct (Graph, lecture seule), fusionnés avec ceux des calendriers EDS
 * par MainWindow::LoadEvents (MainWindow.Events.cpp). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    namespace
    {
        /** @brief Personne choisie dans la boîte : nom affiché et groupe de calendriers. L'adresse est la clé de la table. */
        struct Chosen
        {
            std::wstring name;
            std::wstring group;
        };
        using Selection = std::map<std::wstring, Chosen>;

        /**
         * @brief Ajoute une case à cocher « personne » dans une liste, cochée si déjà choisie.
         * @param list Panneau recevant la case.
         * @param selection Choix en cours (adresse -> nom), partagé avec les rappels.
         * @param email Adresse de la personne.
         * @param name Nom affiché.
         * @param detail Précision affichée après le nom (relation, fonction), éventuellement vide.
         * @param group_of Fournit le groupe de calendriers au moment où la case est cochée.
         */
        void AppendPersonCheckBox(StackPanel const& list, std::shared_ptr<Selection> const& selection,
            hstring const& email, hstring const& name, hstring const& detail, std::function<std::wstring()> group_of)
        {
            CheckBox box;
            std::wstring label{ name.empty() ? email : name };
            if (!detail.empty())
                label += L" — " + std::wstring{ detail };
            box.Content(box_value(hstring{ label }));
            box.IsChecked(selection->count(std::wstring{ email }) > 0);
            ToolTipService::SetToolTip(box, box_value(email));

            std::wstring key{ email };
            std::wstring display{ name.empty() ? email : name };
            box.Click([selection, key, display, group_of](IInspectable const& sender, RoutedEventArgs const&)
            {
                auto check = sender.as<CheckBox>();
                if (check.IsChecked() && check.IsChecked().GetBoolean())
                    (*selection)[key] = Chosen{ display, group_of() };
                else
                    selection->erase(key);
            });
            list.Children().Append(box);
        }

        /**
         * @brief Titre de section de la boîte (texte en gras).
         * @param text Texte du titre.
         * @return Bloc de texte.
         */
        TextBlock SectionTitle(hstring const& text)
        {
            TextBlock title;
            title.Text(text);
            title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            title.Margin(ThicknessHelper::FromLengths(0, 8, 0, 2));
            return title;
        }
    }

    /**
     * @brief Compte Microsoft 365 utilisé pour lire les calendriers de personnes et interroger l'annuaire.
     * @return Identifiant de la collection du premier calendrier « microsoft365 » connu ("" si aucun).
     */
    hstring MainWindow::PeopleCalendarAccountUid()
    {
        for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"backend", L"") == L"microsoft365")
                return source.GetNamedString(L"account_uid", L"");
        }
        return hstring{};
    }

    /**
     * @brief Boîte « Calendriers de personnes et d'équipes » : ajoute ou retire des calendriers
     *        de collègues (mon équipe, recherche dans l'annuaire). Les calendriers cochés sont
     *        mémorisés, affichés dans la vue Calendrier et lus en direct (lecture seule) ; une
     *        personne n'apparaît que si elle a partagé son calendrier avec l'utilisateur.
     */
    fire_and_forget MainWindow::ShowPeopleCalendarsDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !m_core || !Content() || !Content().XamlRoot())
            co_return;

        hstring account_uid = PeopleCalendarAccountUid();

        /* choix en cours : personnes déjà mémorisées */
        auto selection = std::make_shared<Selection>();
        std::map<std::wstring, std::wstring> colors;
        for (auto const& value : ::EvolutionWin::People::Load())
        {
            if (value.ValueType() != JsonValueType::Object)
                continue;
            auto person = value.GetObject();
            std::wstring email{ person.GetNamedString(L"email", L"") };
            if (email.empty())
                continue;
            (*selection)[email] = Chosen{ std::wstring{ person.GetNamedString(L"name", hstring{ email }) },
                                         std::wstring{ person.GetNamedString(L"group", L"") } };
            colors[email] = std::wstring{ person.GetNamedString(L"color", L"") };
        }

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(Tr(L"Calendriers de personnes et d'équipes", L"People and team calendars")));
        dialog.PrimaryButtonText(Tr(L"Afficher la sélection", L"Show selection"));
        dialog.CloseButtonText(Tr(L"Annuler", L"Cancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(6);
        panel.MinWidth(460);

        TextBlock intro;
        intro.TextWrapping(TextWrapping::Wrap);
        intro.Text(Tr(
            L"Cochez les collègues dont vous voulez voir le calendrier. Un calendrier n'est lisible que si la personne "
            L"l'a partagé avec vous ; sinon un message l'indique.",
            L"Check the colleagues whose calendar you want to see. A calendar can only be read if the person shared it "
            L"with you; otherwise a message says so."));
        panel.Children().Append(intro);

        if (account_uid.empty())
        {
            TextBlock none;
            none.TextWrapping(TextWrapping::Wrap);
            none.Text(Tr(L"Aucun compte Microsoft 365 / Exchange Online configuré.",
                         L"No Microsoft 365 / Exchange Online account configured."));
            panel.Children().Append(none);
        }

        /* import des groupes d'Outlook (équipes, salles, Team...) lus par EWS */
        auto import_requested = std::make_shared<bool>(false);
        if (!account_uid.empty())
        {
            Button import_button;
            import_button.Content(box_value(Tr(L"Importer mes groupes Outlook (équipes, salles…)", L"Import my Outlook groups (teams, rooms…)")));
            import_button.HorizontalAlignment(HorizontalAlignment::Stretch);
            ToolTipService::SetToolTip(import_button, box_value(Tr(
                L"Lit les groupes de calendriers de votre boîte Outlook (nécessite une autorisation Microsoft la première fois)",
                L"Reads the calendar groups of your Outlook mailbox (needs a Microsoft authorization the first time)")));
            import_button.Click([import_requested, dialog](IInspectable const&, RoutedEventArgs const&)
            {
                *import_requested = true;
                dialog.Hide();
            });
            panel.Children().Append(import_button);
        }

        /* personnes déjà ajoutées */
        StackPanel added_list;
        added_list.Spacing(2);
        if (!selection->empty())
        {
            panel.Children().Append(SectionTitle(Tr(L"Calendriers déjà ajoutés", L"Calendars already added")));
            for (auto const& [email, chosen] : *selection)
            {
                std::wstring group = chosen.group;
                AppendPersonCheckBox(added_list, selection, hstring{ email }, hstring{ chosen.name },
                    hstring{ group }, [group]() { return group; });
            }
            panel.Children().Append(added_list);
        }

        /* mon équipe (responsable, collaborateurs, collègues) */
        panel.Children().Append(SectionTitle(Tr(L"Mon équipe", L"My team")));
        StackPanel team_list;
        team_list.Spacing(2);
        TextBlock team_status;
        team_status.Text(Tr(L"Chargement de l'équipe…", L"Loading the team…"));
        team_list.Children().Append(team_status);
        panel.Children().Append(team_list);

        if (!account_uid.empty())
        {
            m_core->ListTeam(account_uid, [this, team_list, team_status, selection](hstring const& json, hstring const& error)
            {
                JsonArray members;
                if (!error.empty() || !JsonArray::TryParse(json, members) || members.Size() == 0)
                {
                    team_status.Text(error.empty() ? Tr(L"Aucune équipe trouvée.", L"No team found.") : error);
                    return;
                }
                team_list.Children().Clear();

                hstring manager_label = Tr(L"responsable", L"manager");
                hstring peer_label = Tr(L"même responsable", L"same manager");

                /* groupes comme dans Outlook : « Équipe : <responsable> » (responsable et collègues)
                   et « Mon équipe » (collaborateurs directs) */
                std::wstring manager_name;
                for (auto const& value : members)
                    if (value.GetObject().GetNamedString(L"relation", L"") == L"manager")
                        manager_name = std::wstring{ value.GetObject().GetNamedString(L"name", L"") };
                std::wstring manager_group = std::wstring{ Tr(L"Équipe : ", L"Team: ") } + manager_name;
                std::wstring reports_group{ Tr(L"Mon équipe", L"My team") };

                for (auto const& value : members)
                {
                    auto member = value.GetObject();
                    hstring relation = member.GetNamedString(L"relation", L"");
                    std::wstring group = relation == L"report" ? reports_group
                        : (manager_name.empty() ? reports_group : manager_group);
                    AppendPersonCheckBox(team_list, selection, member.GetNamedString(L"email", L""),
                        member.GetNamedString(L"name", L""),
                        relation == L"manager" ? manager_label : (relation == L"peer" ? peer_label : hstring{}),
                        [group]() { return group; });
                }
            });
        }

        /* recherche dans l'annuaire */
        panel.Children().Append(SectionTitle(Tr(L"Ajouter depuis l'annuaire", L"Add from the directory")));
        /* groupe de calendriers : groupes Outlook du compte, ou nom libre */
        ComboBox group_box;
        group_box.IsEditable(true);
        group_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        group_box.Header(box_value(Tr(L"Groupe des personnes ajoutées par la recherche (choisir ou saisir un nom)",
                                      L"Group for people added by the search (pick or type a name)")));
        for (auto const& value : m_calendar_groups ? m_calendar_groups : JsonArray{})
        {
            hstring display = ::EvolutionWin::People::GroupDisplayName(value.GetObject());
            if (!display.empty())
                group_box.Items().Append(box_value(display));
        }
        group_box.Text(Tr(L"Autres calendriers", L"Other calendars"));
        panel.Children().Append(group_box);
        TextBox search_box;
        search_box.PlaceholderText(Tr(L"Nom ou adresse (3 caractères au moins)", L"Name or address (at least 3 characters)"));
        panel.Children().Append(search_box);
        StackPanel results_list;
        results_list.Spacing(2);
        ScrollViewer results_scroll;
        results_scroll.MaxHeight(200);
        results_scroll.Content(results_list);
        panel.Children().Append(results_scroll);

        auto search_serial = std::make_shared<uint64_t>(0);
        if (!account_uid.empty())
        {
            search_box.TextChanged([this, account_uid, results_list, selection, search_serial, group_box](IInspectable const& sender, TextChangedEventArgs const&)
            {
                hstring text = sender.as<TextBox>().Text();
                uint64_t serial = ++*search_serial;
                results_list.Children().Clear();
                if (text.size() < 3)
                    return;

                TextBlock waiting;
                waiting.Text(Tr(L"Recherche…", L"Searching…"));
                results_list.Children().Append(waiting);

                m_core->SearchGalAccount(account_uid, text, 15, [results_list, selection, search_serial, serial, group_box](hstring const& json, hstring const& error)
                {
                    if (serial != *search_serial)
                        return;   /* résultat périmé : la saisie a changé */
                    results_list.Children().Clear();

                    JsonArray people;
                    if (!error.empty() || !JsonArray::TryParse(json, people) || people.Size() == 0)
                    {
                        TextBlock none;
                        none.TextWrapping(TextWrapping::Wrap);
                        none.Text(error.empty() ? Tr(L"Aucun résultat dans l'annuaire.", L"No results in the directory.") : error);
                        results_list.Children().Append(none);
                        return;
                    }
                    for (auto const& value : people)
                    {
                        auto person = value.GetObject();
                        AppendPersonCheckBox(results_list, selection, person.GetNamedString(L"email", L""),
                            person.GetNamedString(L"name", L""), person.GetNamedString(L"title", L""),
                            [group_box]()
                            {
                                std::wstring text{ group_box.Text() };
                                return text.empty() ? std::wstring{ Tr(L"Autres calendriers", L"Other calendars") } : text;
                            });
                    }
                });
            });
        }

        ScrollViewer outer;
        outer.MaxHeight(560);
        outer.Content(panel);
        dialog.Content(outer);

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (*import_requested)
        {
            ImportOutlookGroupsAsync();
            co_return;
        }
        if (result != ContentDialogResult::Primary)
            co_return;

        /* enregistre la sélection : nouvelles personnes affichées, retirées masquées */
        auto& settings = ::EvolutionWin::Settings::Instance();
        JsonArray previous_visible = settings.GetArray(L"calendar.visible");
        JsonArray visible;
        for (auto const& entry : previous_visible)
        {
            if (entry.ValueType() != JsonValueType::String)
                continue;
            hstring uid = entry.GetString();
            hstring email = ::EvolutionWin::People::EmailFromUid(uid);
            if (email.empty() || selection->count(std::wstring{ email }) > 0)
                visible.Append(entry);
        }

        JsonArray people;
        uint32_t index = 0;
        for (auto const& [email, chosen] : *selection)
        {
            JsonObject person;
            auto known = colors.find(email);
            person.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
            person.SetNamedValue(L"name", JsonValue::CreateStringValue(chosen.name));
            person.SetNamedValue(L"group", JsonValue::CreateStringValue(chosen.group));
            person.SetNamedValue(L"color", JsonValue::CreateStringValue(
                known != colors.end() && !known->second.empty() ? hstring{ known->second } : ::EvolutionWin::People::ColorFor(index)));
            person.SetNamedValue(L"account_uid", JsonValue::CreateStringValue(account_uid));
            people.Append(person);
            ++index;

            if (known == colors.end())
                visible.Append(JsonValue::CreateStringValue(::EvolutionWin::People::UidFor(hstring{ email })));
        }
        ::EvolutionWin::People::Save(people);
        settings.Set(L"calendar.visible", visible);

        /* sources virtuelles : retire les anciennes, ajoute les nouvelles */
        RemergeCalendarSources();

        ShowStatus(Tr(L"Calendriers de personnes mis à jour.", L"People calendars updated."), InfoBarSeverity::Success);
        RebuildCalendarSourcesPanel();
        LoadEvents();
    }
}

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Mémorise les calendriers des groupes d'Outlook lus par EWS et les affiche dans leurs groupes.
     * @param groups Résultat de evo_core_list_nav_calendars : [{"name", "calendars": [{"name", "email"}]}].
     * @param account_uid Compte Microsoft 365 concerné.
     * @param announce true pour indiquer le résultat dans la barre d'état.
     */
    void MainWindow::ApplyOutlookGroups(JsonArray const& groups, hstring const& account_uid, bool announce)
    {
        JsonArray nav;
        uint32_t index = 0, groups_count = 0;
        for (auto const& group_value : groups)
        {
            auto group = group_value.GetObject();
            hstring group_name = group.GetNamedString(L"name", L"");
            /* noms standard (boîte non francisée) : mêmes libellés que dans Outlook en français */
            if (group_name == L"My Calendars")
                group_name = L"Mes calendriers";
            else if (group_name == L"Other Calendars")
                group_name = L"Autres calendriers";
            else if (group_name == L"People's Calendars")
                group_name = L"Calendriers des contacts";
            bool counted = false;
            for (auto const& calendar_value : group.GetNamedArray(L"calendars", JsonArray{}))
            {
                auto calendar = calendar_value.GetObject();
                hstring email = calendar.GetNamedString(L"email", L"");
                if (email.empty())
                    continue;   /* adresse introuvable dans l'annuaire : calendrier non lisible */
                JsonObject person;
                person.SetNamedValue(L"email", JsonValue::CreateStringValue(email));
                person.SetNamedValue(L"name", JsonValue::CreateStringValue(calendar.GetNamedString(L"name", email)));
                person.SetNamedValue(L"group", JsonValue::CreateStringValue(group_name));
                person.SetNamedValue(L"color", JsonValue::CreateStringValue(::EvolutionWin::People::ColorFor(index++)));
                person.SetNamedValue(L"account_uid", JsonValue::CreateStringValue(account_uid));
                nav.Append(person);
                counted = true;
            }
            if (counted)
                ++groups_count;
        }

        ::EvolutionWin::People::SaveNav(nav);
        ::EvolutionWin::Settings::Instance().SetBoolean(L"calendar.nav-import", true);
        RemergeCalendarSources();
        RebuildCalendarSourcesPanel();

        if (announce)
            ShowStatus(Tr(L"Groupes Outlook importés : ", L"Outlook groups imported: ") + to_hstring(nav.Size()) +
                Tr(L" calendriers dans ", L" calendars in ") + to_hstring(groups_count) + Tr(L" groupes.", L" groups."),
                InfoBarSeverity::Success);
    }

    /**
     * @brief Importe les groupes de calendriers d'Outlook (équipes, salles, groupes personnels) lus par EWS.
     *        Si Microsoft n'a pas encore autorisé l'application, propose la page d'autorisation.
     */
    fire_and_forget MainWindow::ImportOutlookGroupsAsync()
    {
        auto strong = get_strong();
        hstring account_uid = PeopleCalendarAccountUid();
        if (account_uid.empty())
            co_return;

        ShowStatus(Tr(L"Lecture des groupes de calendriers Outlook…", L"Reading the Outlook calendar groups…"), InfoBarSeverity::Informational);
        m_core->ListNavCalendars(account_uid, [this, account_uid](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                std::wstring text{ error };
                if (text.find(L"AADSTS65001") != std::wstring::npos || text.find(L"consent") != std::wstring::npos)
                    ShowEwsConsentDialogAsync();
                else
                    ShowStatus(Tr(L"Groupes Outlook : ", L"Outlook groups: ") + error, InfoBarSeverity::Error);
                return;
            }
            JsonArray groups;
            if (!JsonArray::TryParse(json, groups))
            {
                ShowStatus(Tr(L"Groupes Outlook : réponse illisible.", L"Outlook groups: unreadable response."), InfoBarSeverity::Error);
                return;
            }
            ApplyOutlookGroups(groups, account_uid, true);
        });
        co_return;
    }

    /**
     * @brief Boîte d'autorisation : ouvre la page Microsoft qui accorde l'accès EWS (une seule fois),
     *        puis relance l'import. Selon la stratégie de l'organisation, l'accord d'un administrateur
     *        peut être nécessaire.
     */
    fire_and_forget MainWindow::ShowEwsConsentDialogAsync()
    {
        auto strong = get_strong();
        if (!Content() || !Content().XamlRoot())
            co_return;

        hstring account_uid = PeopleCalendarAccountUid();
        while (!m_dialog_open)
        {
            ContentDialog dialog;
            dialog.XamlRoot(Content().XamlRoot());
            dialog.Title(box_value(Tr(L"Autorisation Microsoft requise", L"Microsoft authorization required")));
            TextBlock text;
            text.TextWrapping(TextWrapping::Wrap);
            text.MaxWidth(520);
            text.Text(Tr(
                L"Pour lire vos groupes de calendriers Outlook (équipes, salles, groupes personnels), l'application doit être autorisée une fois "
                L"à accéder à Exchange Online.\n\n1. Cliquez sur « Ouvrir la page d'autorisation » et acceptez dans votre navigateur "
                L"(la page se termine sur une page blanche, c'est normal).\n2. Revenez ici et cliquez sur « J'ai autorisé : réessayer ».\n\n"
                L"Si Microsoft indique qu'un administrateur doit approuver l'application, transmettez-lui la demande : "
                L"l'import ne sera possible qu'après son accord.",
                L"To read your Outlook calendar groups (teams, rooms, personal groups), the application must be authorized once to access "
                L"Exchange Online.\n\n1. Click \"Open the authorization page\" and accept in your browser (the page ends on a blank page, "
                L"this is normal).\n2. Come back and click \"I authorized: retry\".\n\n"
                L"If Microsoft says an administrator must approve the application, forward the request: the import will only work after approval."));
            dialog.Content(text);
            dialog.PrimaryButtonText(Tr(L"Ouvrir la page d'autorisation", L"Open the authorization page"));
            dialog.SecondaryButtonText(Tr(L"J'ai autorisé : réessayer", L"I authorized: retry"));
            dialog.CloseButtonText(Tr(L"Annuler", L"Cancel"));
            dialog.DefaultButton(ContentDialogButton::Primary);

            m_dialog_open = true;
            auto result = co_await dialog.ShowAsync();
            m_dialog_open = false;

            if (result == ContentDialogResult::Primary)
            {
                m_core->GetEwsConsentUrl(account_uid, [](hstring const& json, hstring const&)
                {
                    JsonObject object;
                    if (JsonObject::TryParse(json, object))
                        Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ object.GetNamedString(L"url", L"") });
                });
                continue;   /* la boîte revient : l'utilisateur coche « réessayer » après avoir autorisé */
            }
            if (result == ContentDialogResult::Secondary)
                ImportOutlookGroupsAsync();
            break;
        }
    }
}
