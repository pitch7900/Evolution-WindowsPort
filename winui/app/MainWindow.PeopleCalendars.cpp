#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "PeopleCalendars.h"

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
