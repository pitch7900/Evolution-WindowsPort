#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "PeopleCalendars.h"
#include <map>

#include <winrt/Windows.System.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <utility>
#include <vector>

/* Volet de sources du calendrier et des contacts (calendriers, carnets
 * d'adresses). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* Volet de sources (calendriers, carnets d'adresses) : groupes par compte ;
     * case à cocher = affiché, clic sur le nom = source active (mise en
     * évidence) ; pastille de couleur si la source en a une.
     * Paramètres :
     *   panel       : StackPanel à remplir
     *   sources     : evo_core_list_sources ()
     *   visible     : uids affichés
     *   active      : uid de la source active
     *   on_toggle   : case cochée / décochée (uid, affiché)
     *   on_activate : clic sur le nom (uid)
     * Retourne : nom de la source active. */
    static hstring BuildSourcesPanel(StackPanel const& panel, JsonArray const& sources,
        std::vector<hstring> const& visible, hstring const& active,
        std::function<void(hstring const&, bool)> on_toggle, std::function<void(hstring const&)> on_activate,
        std::function<void(StackPanel const&, JsonObject const&)> group_footer = nullptr,
        wchar_t const* collapse_key = L"sources.collapsed",
        std::function<void(std::vector<hstring> const&, bool)> on_toggle_group = nullptr)
    {
        auto children = panel.Children();
        children.Clear();
        hstring active_name;

        /* un groupe par compte, et par groupe de calendriers Exchange Online */
        auto group_title = [](JsonObject const& source)
        {
            hstring account = source.GetNamedString(L"account", L"");
            hstring group = source.GetNamedString(L"group_name", L"");
            return group.empty() ? account : account + L" \u203A " + group;
        };

        /* titres : d'abord les comptes, puis leurs groupes (affichés en retrait sous leur compte) */
        std::vector<hstring> accounts;
        for (auto const& value : sources)
        {
            hstring account = value.GetObject().GetNamedString(L"account", L"");
            if (std::find(accounts.begin(), accounts.end(), account) == accounts.end())
                accounts.push_back(account);
        }
        for (auto const& value : sources)
        {
            hstring title = group_title(value.GetObject());
            if (std::find(accounts.begin(), accounts.end(), title) == accounts.end())
                accounts.push_back(title);
        }
        std::map<hstring, StackPanel> parent_items;   /* compte -> liste de ses éléments (reçoit ses groupes) */

        /* groupes repliés (mémorisés par titre de groupe) */
        auto& collapse_settings = ::EvolutionWin::Settings::Instance();
        auto collapsed_titles = std::make_shared<std::vector<hstring>>();
        for (auto const& entry : collapse_settings.GetArray(collapse_key))
            if (entry.ValueType() == JsonValueType::String)
                collapsed_titles->push_back(entry.GetString());

        for (auto const& account : accounts)
        {
            bool collapsed = std::find(collapsed_titles->begin(), collapsed_titles->end(), account) != collapsed_titles->end();

            /* groupe d'un compte (« compte › groupe ») : placé sous son compte, nom du groupe seul */
            hstring sub_name, parent_account;
            for (auto const& value : sources)
            {
                auto member = value.GetObject();
                if (!member.GetNamedString(L"group_name", L"").empty() && group_title(member) == account)
                {
                    sub_name = member.GetNamedString(L"group_name", L"");
                    parent_account = member.GetNamedString(L"account", L"");
                    break;
                }
            }
            bool is_sub = !sub_name.empty();
            auto target_children = (is_sub && parent_items.count(parent_account) > 0
                ? parent_items.at(parent_account) : panel).Children();

            /* en-tête cliquable : chevron + titre du groupe, plie / déplie la liste */
            Grid header_row;
            header_row.ColumnSpacing(6);
            header_row.Margin(ThicknessHelper::FromLengths(4, 8, 0, 2));
            header_row.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            ColumnDefinition chevron_col;
            chevron_col.Width(GridLengthHelper::Auto());
            header_row.ColumnDefinitions().Append(chevron_col);
            ColumnDefinition group_check_col;
            group_check_col.Width(GridLengthHelper::Auto());
            header_row.ColumnDefinitions().Append(group_check_col);
            header_row.ColumnDefinitions().Append(ColumnDefinition{});

            FontIcon chevron;
            chevron.Glyph(collapsed ? L"\uE76C" : L"\uE70D");
            chevron.FontSize(10);
            chevron.VerticalAlignment(VerticalAlignment::Center);
            header_row.Children().Append(chevron);

            /* case du groupe : coche / décoche tous ses éléments d'un coup (état partiel si mélangé) */
            std::vector<hstring> group_uids;
            for (auto const& value : sources)
            {
                auto member = value.GetObject();
                if (member.GetNamedBoolean(L"placeholder", false))
                    continue;
                /* un compte coche aussi ses groupes ; un groupe, ses seuls calendriers */
                if (is_sub ? group_title(member) == account : member.GetNamedString(L"account", L"") == account)
                    group_uids.push_back(member.GetNamedString(L"uid", L""));
            }
            if (on_toggle_group && !group_uids.empty())
            {
                size_t shown_count = 0;
                for (auto const& uid : group_uids)
                    if (std::find(visible.begin(), visible.end(), uid) != visible.end())
                        ++shown_count;

                CheckBox group_check;
                group_check.MinWidth(0);
                group_check.Padding(ThicknessHelper::FromLengths(0, 0, 0, 0));
                group_check.VerticalAlignment(VerticalAlignment::Center);
                if (shown_count == group_uids.size() && !group_uids.empty())
                    group_check.IsChecked(true);
                else if (shown_count == 0)
                    group_check.IsChecked(false);
                else
                    group_check.IsChecked(Windows::Foundation::IReference<bool>(nullptr));
                bool all_shown = shown_count == group_uids.size() && !group_uids.empty();
                ToolTipService::SetToolTip(group_check, box_value(L"Afficher / masquer tout le groupe"));
                group_check.Tapped([](IInspectable const&, Input::TappedRoutedEventArgs const& args) { args.Handled(true); });
                group_check.Click([group_uids, all_shown, on_toggle_group](IInspectable const&, RoutedEventArgs const&)
                {
                    on_toggle_group(group_uids, !all_shown);
                });
                Grid::SetColumn(group_check, 1);
                header_row.Children().Append(group_check);
            }

            TextBlock header;
            header.Text(is_sub ? sub_name : (account.empty() ? hstring{ L"Autres" } : account));
            header.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            header.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
            header.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(header, 2);
            header_row.Children().Append(header);
            ToolTipService::SetToolTip(header_row, box_value(L"Plier / déplier"));
            target_children.Append(header_row);

            StackPanel group_items;
            group_items.Margin(ThicknessHelper::FromLengths(18, 0, 0, 0));   /* éléments en retrait sous le groupe */
            group_items.Visibility(collapsed ? Visibility::Collapsed : Visibility::Visible);
            target_children.Append(group_items);
            if (!is_sub)
                parent_items.insert_or_assign(account, group_items);

            header_row.Tapped([group_items, chevron, account, collapsed_titles, collapse_key](IInspectable const&, Input::TappedRoutedEventArgs const&)
            {
                bool now_collapsed = group_items.Visibility() == Visibility::Visible;
                group_items.Visibility(now_collapsed ? Visibility::Collapsed : Visibility::Visible);
                chevron.Glyph(now_collapsed ? L"\uE76C" : L"\uE70D");

                auto it = std::find(collapsed_titles->begin(), collapsed_titles->end(), account);
                if (now_collapsed && it == collapsed_titles->end())
                    collapsed_titles->push_back(account);
                else if (!now_collapsed && it != collapsed_titles->end())
                    collapsed_titles->erase(it);

                JsonArray stored;
                for (auto const& title : *collapsed_titles)
                    stored.Append(JsonValue::CreateStringValue(title));
                ::EvolutionWin::Settings::Instance().Set(collapse_key, stored);
            });

            if (group_uids.empty())
            {
                TextBlock empty_hint;
                empty_hint.Text(L"Aucun calendrier : Outlook n'expose pas les membres de ce groupe. Ajoutez des personnes avec « Personnes et équipes… ».");
                empty_hint.TextWrapping(TextWrapping::Wrap);
                empty_hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
                empty_hint.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorTertiaryBrush")).as<Brush>());
                empty_hint.Margin(ThicknessHelper::FromLengths(4, 0, 4, 4));
                group_items.Children().Append(empty_hint);
            }

            for (auto const& value : sources)
            {
                auto source = value.GetObject();
                if (group_title(source) != account || source.GetNamedBoolean(L"placeholder", false))
                    continue;

                hstring uid = source.GetNamedString(L"uid", L"");
                hstring name = source.GetNamedString(L"name", L"");
                bool is_active = uid == active;
                if (is_active)
                    active_name = name;

                Grid row;
                row.ColumnSpacing(6);
                row.Padding(ThicknessHelper::FromLengths(4, 0, 4, 0));
                row.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                ColumnDefinition check_col;
                check_col.Width(GridLengthHelper::Auto());
                row.ColumnDefinitions().Append(check_col);
                row.ColumnDefinitions().Append(ColumnDefinition{});
                if (is_active)
                    row.Background(Application::Current().Resources().Lookup(box_value(L"SubtleFillColorSecondaryBrush")).as<Brush>());

                CheckBox shown;
                shown.MinWidth(0);
                shown.IsChecked(std::find(visible.begin(), visible.end(), uid) != visible.end());
                ToolTipService::SetToolTip(shown, box_value(L"Afficher"));
                shown.Click([uid, on_toggle](IInspectable const& sender, RoutedEventArgs const&)
                {
                    auto box = sender.as<CheckBox>();
                    on_toggle(uid, box.IsChecked() && box.IsChecked().GetBoolean());
                });
                row.Children().Append(shown);

                StackPanel label;
                label.Orientation(Orientation::Horizontal);
                label.Spacing(8);
                label.VerticalAlignment(VerticalAlignment::Center);
                label.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });

                std::wstring color{ source.GetNamedString(L"color", L"") };
                if (color.size() == 7 && color[0] == L'#')
                {
                    Border dot;
                    dot.Width(12);
                    dot.Height(12);
                    dot.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
                    auto channel = [&](size_t at) { return static_cast<uint8_t>(std::wcstoul(color.substr(at, 2).c_str(), nullptr, 16)); };
                    dot.Background(SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, channel(1), channel(3), channel(5)) });
                    label.Children().Append(dot);
                }

                TextBlock text;
                text.Text(name);
                text.TextTrimming(TextTrimming::CharacterEllipsis);
                if (is_active)
                    text.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                label.Children().Append(text);
                ToolTipService::SetToolTip(label, box_value(is_active
                    ? hstring{ L"Actif : les nouveaux éléments y sont créés" }
                    : hstring{ L"Choisir pour les nouveaux éléments" }));
                label.Tapped([uid, on_activate](IInspectable const&, Input::TappedRoutedEventArgs const&)
                {
                    on_activate(uid);
                });
                Grid::SetColumn(label, 1);
                row.Children().Append(label);

                group_items.Children().Append(row);
            }

            /* pied de groupe (ex. « Annuaire de l'organisation » du compte) */
            if (group_footer)
            {
                for (auto const& value : sources)
                {
                    auto source = value.GetObject();
                    if (group_title(source) == account && source.GetNamedString(L"group_name", L"").empty())
                    {
                        group_footer(group_items, source);
                        break;
                    }
                }
            }
        }

        return active_name;
    }

    /* uids affichés de la préférence key, parmi les sources connues */
    static std::vector<hstring> VisibleUids(JsonArray const& sources, wchar_t const* key)
    {
        std::vector<hstring> uids;
        auto visible = ::EvolutionWin::Settings::Instance().GetArray(key);
        for (auto const& value : sources ? sources : JsonArray{})
        {
            hstring uid = value.GetObject().GetNamedString(L"uid", L"");
            for (auto const& entry : visible)
                if (entry.ValueType() == JsonValueType::String && entry.GetString() == uid)
                    uids.push_back(uid);
        }
        return uids;
    }

    /* source active de la préférence key, sinon celle par défaut */
    static hstring ActiveUid(JsonArray const& sources, wchar_t const* key)
    {
        hstring active = ::EvolutionWin::Settings::Instance().GetString(key, L"");
        hstring fallback;
        for (auto const& value : sources ? sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"uid", L"") == active)
                return active;
            if (source.GetNamedBoolean(L"default", false))
                fallback = source.GetNamedString(L"uid", L"");
        }
        return fallback;
    }

    /* coche / décoche uid dans la préférence key */
    static void SetUidVisible(JsonArray const& sources, wchar_t const* key, hstring const& uid, bool shown)
    {
        JsonArray updated;
        for (auto const& current : VisibleUids(sources, key))
            if (current != uid)
                updated.Append(JsonValue::CreateStringValue(current));
        if (shown)
            updated.Append(JsonValue::CreateStringValue(uid));
        ::EvolutionWin::Settings::Instance().Set(key, updated);
    }

    /* Liste des calendriers (comptes, couleurs, calendrier par défaut), puis
     * événements des calendriers affichés. */
    /* Rend visibles les calendriers Exchange Online (et Exchange) encore jamais vus : ceux que
     * l'utilisateur a déjà ajoutés à son compte (calendriers partagés, de personnes, d'équipes)
     * apparaissent d'eux-mêmes ; un calendrier décoché ensuite le reste (il est alors « connu »).
     * Paramètres :
     *   sources : calendriers renvoyés par evo_core_list_sources () */
    static void AutoShowExchangeCalendars(JsonArray const& sources)
    {
        auto& settings = ::EvolutionWin::Settings::Instance();
        std::vector<hstring> known;
        for (auto const& entry : settings.GetArray(L"calendar.known"))
            if (entry.ValueType() == JsonValueType::String)
                known.push_back(entry.GetString());

        JsonArray visible = settings.GetArray(L"calendar.visible");
        auto is_visible = [&visible](hstring const& uid)
        {
            for (auto const& entry : visible)
                if (entry.ValueType() == JsonValueType::String && entry.GetString() == uid)
                    return true;
            return false;
        };

        bool changed = false;
        JsonArray updated_known;
        for (auto const& value : sources)
        {
            auto source = value.GetObject();
            hstring uid = source.GetNamedString(L"uid", L"");
            hstring backend = source.GetNamedString(L"backend", L"");
            updated_known.Append(JsonValue::CreateStringValue(uid));
            if (std::find(known.begin(), known.end(), uid) != known.end())
                continue;
            if ((backend == L"microsoft365" || backend == L"ews") && !is_visible(uid))
            {
                visible.Append(JsonValue::CreateStringValue(uid));
                changed = true;
            }
        }

        settings.Set(L"calendar.known", updated_known);
        if (changed)
            settings.Set(L"calendar.visible", visible);
    }

    void MainWindow::LoadCalendarSources()
    {
        m_core->ListSources(L"calendar", [this](hstring const& json, hstring const& error)
        {
            JsonArray sources;
            if (!error.empty() || !JsonArray::TryParse(json, sources))
            {
                ShowStatus(L"Calendriers : " + error, InfoBarSeverity::Error);
                return;
            }

            m_calendar_sources = ::EvolutionWin::People::MergeIntoSources(sources, m_calendar_groups);

            /* premier usage : calendrier par défaut et calendriers principaux
               des comptes, pas les calendriers partagés d'autres personnes */
            auto& settings = ::EvolutionWin::Settings::Instance();
            if (!settings.GetBoolean(L"calendar.visible-init", false))
            {
                JsonArray visible;
                for (auto const& value : sources)
                {
                    auto source = value.GetObject();
                    hstring name = source.GetNamedString(L"name", L"");
                    if (source.GetNamedBoolean(L"default", false) || name == L"Calendrier" || name == L"Calendar" ||
                        name == L"Personnel" || name == L"Personal")
                        visible.Append(JsonValue::CreateStringValue(source.GetNamedString(L"uid", L"")));
                }
                settings.Set(L"calendar.visible", visible);
                settings.SetBoolean(L"calendar.visible-init", true);
            }
            AutoShowExchangeCalendars(sources);

            RebuildCalendarSourcesPanel();
            LoadEvents();

            /* groupes de calendriers Outlook (Équipe, Salles, Team...) : noms seulement */
            if (hstring account_uid = PeopleCalendarAccountUid(); !account_uid.empty())
            {
                m_core->ListCalendarGroups(account_uid, [this](hstring const& groups_json, hstring const& groups_error)
                {
                    JsonArray groups;
                    if (!groups_error.empty() || !JsonArray::TryParse(groups_json, groups))
                        return;
                    m_calendar_groups = groups;
                    RemergeCalendarSources();
                    RebuildCalendarSourcesPanel();
                });
            }
        });
    }

    /* Reconstruit m_calendar_sources : calendriers EDS (sans repères ni personnes) fusionnés de nouveau
     * avec les groupes Outlook et les calendriers de personnes. */
    void MainWindow::RemergeCalendarSources()
    {
        JsonArray real;
        for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
        {
            hstring backend = value.GetObject().GetNamedString(L"backend", L"");
            if (backend != L"person" && backend != L"group")
                real.Append(value);
        }
        m_calendar_sources = ::EvolutionWin::People::MergeIntoSources(real, m_calendar_groups);
    }

    std::vector<hstring> MainWindow::VisibleCalendarUids()
    {
        return VisibleUids(m_calendar_sources, L"calendar.visible");
    }

    /* Bouton « + » du volet des calendriers : ouvre la boîte d'ajout d'un calendrier. */
    void MainWindow::OnAddCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAddCalendarDialogAsync();
    }

    hstring MainWindow::ActiveCalendarUid()
    {
        return ActiveUid(m_calendar_sources, L"calendar.active");
    }

    void MainWindow::RebuildCalendarSourcesPanel()
    {
        hstring active_name = BuildSourcesPanel(CalendarSourcesPanel(), m_calendar_sources,
            VisibleCalendarUids(), ActiveCalendarUid(),
            [this](hstring const& uid, bool shown)
            {
                SetUidVisible(m_calendar_sources, L"calendar.visible", uid, shown);
                LoadEvents();
            },
            [this](hstring const& uid)
            {
                ::EvolutionWin::Settings::Instance().SetString(L"calendar.active", uid);
                RebuildCalendarSourcesPanel();
            },
            nullptr, L"calendar.collapsed",
            [this](std::vector<hstring> const& uids, bool shown)
            {
                for (auto const& uid : uids)
                    SetUidVisible(m_calendar_sources, L"calendar.visible", uid, shown);
                RebuildCalendarSourcesPanel();
                LoadEvents();
            });

        ActiveCalendarText().Text(active_name.empty() ? hstring{}
            : L"Nouveaux événements dans : " + active_name);
    }

    /* Carnets d'adresses (comptes, carnet par défaut), puis contacts des
     * carnets affichés ; premier usage : carnet par défaut et carnets
     * principaux des comptes, pas les annuaires d'entreprise. */
    void MainWindow::LoadContactBooks()
    {
        m_core->ListSources(L"contacts", [this](hstring const& json, hstring const& error)
        {
            JsonArray books;
            if (!error.empty() || !JsonArray::TryParse(json, books))
            {
                ShowStatus(L"Carnets d'adresses : " + error, InfoBarSeverity::Error);
                return;
            }

            m_contact_books = books;

            auto& settings = ::EvolutionWin::Settings::Instance();
            if (!settings.GetBoolean(L"contacts.visible-init", false))
            {
                JsonArray visible;
                for (auto const& value : books)
                {
                    auto book = value.GetObject();
                    hstring name = book.GetNamedString(L"name", L"");
                    if (book.GetNamedBoolean(L"default", false) || name == L"Contacts" || name == L"Mes contacts" ||
                        name == L"My Contacts" || name == L"Personnel" || name == L"Personal")
                        visible.Append(JsonValue::CreateStringValue(book.GetNamedString(L"uid", L"")));
                }
                settings.Set(L"contacts.visible", visible);
                settings.SetBoolean(L"contacts.visible-init", true);
            }

            RebuildContactBooksPanel();
            LoadContacts(ContactSearchBox().Text());
        });
    }

    std::vector<hstring> MainWindow::VisibleBookUids()
    {
        return VisibleUids(m_contact_books, L"contacts.visible");
    }

    hstring MainWindow::ActiveBookUid()
    {
        return ActiveUid(m_contact_books, L"contacts.active");
    }

    void MainWindow::RebuildContactBooksPanel()
    {
        int directory_accounts = 0;
        hstring active_name = BuildSourcesPanel(ContactBooksPanel(), m_contact_books,
            VisibleBookUids(), ActiveBookUid(),
            [this](hstring const& uid, bool shown)
            {
                SetUidVisible(m_contact_books, L"contacts.visible", uid, shown);
                LoadContacts(ContactSearchBox().Text());
            },
            [this](hstring const& uid)
            {
                ::EvolutionWin::Settings::Instance().SetString(L"contacts.active", uid);
                RebuildContactBooksPanel();
            },
            [this, &directory_accounts](StackPanel const& panel, JsonObject const& source)
            {
                hstring backend = source.GetNamedString(L"backend", L"");
                hstring account_uid = source.GetNamedString(L"account_uid", L"");
                if ((backend != L"microsoft365" && backend != L"google") || account_uid.empty())
                    return;
                ++directory_accounts;

                HyperlinkButton directory;
                directory.Content(box_value(::EvolutionWin::Helpers::Tr(L"Annuaire de l'organisation", L"Organization directory")));
                directory.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
                directory.Click([this, account_uid](IInspectable const&, RoutedEventArgs const&)
                {
                    OnGalDirectoryForAccount(account_uid);
                });
                panel.Children().Append(directory);
            },
            L"contacts.collapsed",
            [this](std::vector<hstring> const& uids, bool shown)
            {
                for (auto const& uid : uids)
                    SetUidVisible(m_contact_books, L"contacts.visible", uid, shown);
                RebuildContactBooksPanel();
                LoadContacts(ContactSearchBox().Text());
            });

        /* le bouton global ne sert que s'il n'y a aucun compte avec annuaire (message explicatif) */
        GalDirectoryButton().Visibility(directory_accounts > 0 ? Visibility::Collapsed : Visibility::Visible);

        ActiveBookText().Text(active_name.empty() ? hstring{}
            : L"Nouveaux contacts dans : " + active_name);
    }

    /* Ajoute un calendrier : calendrier local vide, ou abonnement à une URL
     * iCalendar (.ics / webcal://) actualisé périodiquement. Le nouveau
     * calendrier est affiché immédiatement dans la vue Calendrier. */
    fire_and_forget MainWindow::ShowAddCalendarDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        auto trimmed = [](hstring const& text)
        {
            std::wstring value{ text };
            auto first = value.find_first_not_of(L" \t\r\n");
            auto last = value.find_last_not_of(L" \t\r\n");
            return first == std::wstring::npos ? std::wstring{} : value.substr(first, last - first + 1);
        };

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Ajouter un calendrier"));

        StackPanel panel;
        panel.Spacing(12);
        TextBox name_box;
        name_box.Header(box_value(L"Nom du calendrier"));
        name_box.MinWidth(360);
        panel.Children().Append(name_box);
        TextBox url_box;
        url_box.Header(box_value(L"Adresse Internet (ICS / webcal) — laisser vide pour un calendrier local vide"));
        url_box.PlaceholderText(L"https://exemple.fr/agenda.ics");
        panel.Children().Append(url_box);
        dialog.Content(panel);

        dialog.PrimaryButtonText(L"Ajouter");
        dialog.SecondaryButtonText(L"Depuis Exchange Online…");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);
        dialog.IsPrimaryButtonEnabled(false);
        name_box.TextChanged([dialog, trimmed](IInspectable const& sender, TextChangedEventArgs const&)
        {
            dialog.IsPrimaryButtonEnabled(!trimmed(sender.as<TextBox>().Text()).empty());
        });

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;
        if (result == ContentDialogResult::Secondary)
        {
            ShowAddExchangeCalendarDialogAsync();
            co_return;
        }
        if (result != ContentDialogResult::Primary)
            co_return;

        JsonObject request;
        request.SetNamedValue(L"name", JsonValue::CreateStringValue(trimmed(name_box.Text())));
        request.SetNamedValue(L"url", JsonValue::CreateStringValue(trimmed(url_box.Text())));

        ShowStatus(L"Création du calendrier…", InfoBarSeverity::Informational);
        m_core->CreateCalendar(request.Stringify(), [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Ajout du calendrier : " + error, InfoBarSeverity::Error);
                return;
            }

            /* affiche le nouveau calendrier puis recharge la liste */
            JsonObject created;
            if (JsonObject::TryParse(json, created))
            {
                auto& settings = ::EvolutionWin::Settings::Instance();
                JsonArray visible = settings.GetArray(L"calendar.visible");
                visible.Append(JsonValue::CreateStringValue(created.GetNamedString(L"uid", L"")));
                settings.Set(L"calendar.visible", visible);
            }
            ShowStatus(L"Calendrier ajouté.", InfoBarSeverity::Success);
            LoadCalendarSources();
        });
    }

    /* Titre d'un groupe de sources : compte, suivi du groupe de calendriers
     * Exchange Online (« Équipe de ... ») lorsqu'il existe.
     * Paramètres :
     *   source : objet JSON renvoyé par evo_core_list_sources () */
    static hstring SourceGroupTitle(JsonObject const& source)
    {
        hstring account = source.GetNamedString(L"account", L"");
        hstring group = source.GetNamedString(L"group_name", L"");
        return group.empty() ? account : account + L" \u203A " + group;
    }

    /* Boîte « Calendriers Exchange Online » : liste les calendriers des comptes
     * Exchange Online / Microsoft 365 (les leurs, ceux déjà présents sur le
     * serveur, partagés ou appartenant à un groupe de calendriers), classés par
     * groupe ; les cases cochées sont affichées dans la vue Calendrier. Le
     * bouton de synchronisation demande au serveur de redécouvrir les
     * calendriers puis actualise la liste. */
    fire_and_forget MainWindow::ShowAddExchangeCalendarDialogAsync()
    {
        auto strong = get_strong();
        if (m_dialog_open || !m_core || !Content() || !Content().XamlRoot())
            co_return;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Calendriers Exchange Online"));
        dialog.PrimaryButtonText(L"Afficher la sélection");
        dialog.SecondaryButtonText(L"Personnes et équipes…");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(8);
        panel.MinWidth(420);

        TextBlock intro;
        intro.TextWrapping(TextWrapping::Wrap);
        intro.Text(L"Cochez les calendriers Exchange Online à afficher, y compris ceux des groupes de calendriers "
                   L"(équipes) et les calendriers partagés avec vous.");
        panel.Children().Append(intro);

        StackPanel sync_row;
        sync_row.Orientation(Orientation::Horizontal);
        sync_row.Spacing(8);
        Button sync_btn;
        sync_btn.Content(box_value(L"Synchroniser avec Exchange Online"));
        sync_row.Children().Append(sync_btn);
        ProgressRing ring;
        ring.Width(18);
        ring.Height(18);
        ring.IsActive(false);
        ring.Visibility(Visibility::Collapsed);
        sync_row.Children().Append(ring);
        panel.Children().Append(sync_row);

        StackPanel list_panel;
        list_panel.Spacing(2);
        ScrollViewer scroll;
        scroll.MaxHeight(360);
        scroll.Content(list_panel);
        panel.Children().Append(scroll);
        dialog.Content(panel);

        /* cases à cocher : uid du calendrier -> case */
        auto choices = std::make_shared<std::vector<std::pair<hstring, CheckBox>>>();
        auto fill = [this, list_panel, choices]()
        {
            list_panel.Children().Clear();
            choices->clear();
            auto visible = VisibleCalendarUids();
            std::vector<hstring> titles;
            auto is_exchange = [](JsonObject const& source)
            {
                hstring backend = source.GetNamedString(L"backend", L"");
                return backend == L"microsoft365" || backend == L"ews";
            };

            for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
            {
                auto source = value.GetObject();
                if (!is_exchange(source))
                    continue;
                hstring title = SourceGroupTitle(source);
                if (std::find(titles.begin(), titles.end(), title) == titles.end())
                    titles.push_back(title);
            }

            if (titles.empty())
            {
                TextBlock none;
                none.TextWrapping(TextWrapping::Wrap);
                none.Text(L"Aucun calendrier Exchange Online trouvé. Utilisez le bouton de synchronisation ci-dessus.");
                list_panel.Children().Append(none);
                return;
            }

            for (auto const& title : titles)
            {
                TextBlock header;
                header.Text(title);
                header.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
                header.Margin(ThicknessHelper::FromLengths(0, 8, 0, 2));
                list_panel.Children().Append(header);

                for (auto const& value : m_calendar_sources)
                {
                    auto source = value.GetObject();
                    if (!is_exchange(source) || SourceGroupTitle(source) != title)
                        continue;
                    hstring uid = source.GetNamedString(L"uid", L"");
                    CheckBox box;
                    box.Content(box_value(source.GetNamedString(L"name", L"")));
                    box.IsChecked(std::find(visible.begin(), visible.end(), uid) != visible.end());
                    list_panel.Children().Append(box);
                    choices->emplace_back(uid, box);
                }
            }
        };
        fill();

        sync_btn.Click([this, sync_btn, ring, fill](IInspectable const&, RoutedEventArgs const&)
        {
            sync_btn.IsEnabled(false);
            ring.Visibility(Visibility::Visible);
            ring.IsActive(true);
            m_core->RefreshCalendars([this, sync_btn, ring, fill](hstring const&, hstring const&)
            {
                /* la découverte des calendriers se poursuit côté serveur : courte attente */
                auto timer = std::make_shared<DispatcherTimer>();
                timer->Interval(std::chrono::seconds(5));
                timer->Tick([this, timer, sync_btn, ring, fill](IInspectable const&, IInspectable const&)
                {
                    timer->Stop();
                    m_core->ListSources(L"calendar", [this, sync_btn, ring, fill](hstring const& json, hstring const& error)
                    {
                        JsonArray sources;
                        if (error.empty() && JsonArray::TryParse(json, sources))
                        {
                            AutoShowExchangeCalendars(sources);
                            m_calendar_sources = ::EvolutionWin::People::MergeIntoSources(sources, m_calendar_groups);
                        }
                        fill();
                        ring.IsActive(false);
                        ring.Visibility(Visibility::Collapsed);
                        sync_btn.IsEnabled(true);
                    });
                });
                timer->Start();
            });
        });

        m_dialog_open = true;
        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (result == ContentDialogResult::Secondary)
        {
            ShowPeopleCalendarsDialogAsync();
            co_return;
        }

        if (result == ContentDialogResult::Primary)
        {
            for (auto const& [uid, box] : *choices)
                SetUidVisible(m_calendar_sources, L"calendar.visible", uid, box.IsChecked() && box.IsChecked().GetBoolean());
            ShowStatus(L"Calendriers Exchange Online mis à jour.", InfoBarSeverity::Success);
        }
        RebuildCalendarSourcesPanel();
        LoadEvents();
    }
}
