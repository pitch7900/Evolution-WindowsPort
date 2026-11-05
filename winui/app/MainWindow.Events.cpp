#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "WindowIcon.h"
#include "Settings.h"
#include "CategoryStore.h"
#include "PeopleCalendars.h"

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

/* Calendrier : période affichée, événements (création, modification) et export. */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

namespace winrt::EvolutionWin::implementation
{
    /* onglet Affichage (Calendrier) */
    void MainWindow::OnRibbonTodayClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_selected_day_start = 0;
        MonthCalendar().SelectedDates().Clear();
        MonthCalendar().SetDisplayDate(winrt::clock::now());
        LoadEvents();
    }

    /**
     * Positionne l'affichage du calendrier sur une date précise.
     * Paramètres :
     *   date : date cible sous forme de DateTime WinRT
     */
    void MainWindow::GoToCalendarDate(DateTime date)
    {
        time_t raw = winrt::clock::to_time_t(date);
        struct tm local_tm{};
        localtime_s(&local_tm, &raw);
        local_tm.tm_hour = 0;
        local_tm.tm_min = 0;
        local_tm.tm_sec = 0;
        m_selected_day_start = static_cast<int64_t>(mktime(&local_tm));
        MonthCalendar().SelectedDates().Clear();
        MonthCalendar().SelectedDates().Append(date);
        MonthCalendar().SetDisplayDate(date);
        LoadEvents();
    }

    /**
     * Gestionnaire du clic sur le bouton "Atteindre la date".
     * Ouvre la boîte de dialogue modale permettant de sélectionner une date.
     * Paramètres :
     *   sender : bouton émetteur
     *   args   : arguments d'événement
     */
    void MainWindow::OnGoToDateClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowGoToDateDialogAsync();
    }

    /**
     * Boîte de dialogue "Atteindre une date" (Ctrl+G).
     * Propose un sélecteur de date graphique (CalendarDatePicker) ainsi que
     * des raccourcis rapides ("Aujourd'hui" et "+7 jours").
     */
    fire_and_forget MainWindow::ShowGoToDateDialogAsync()
    {
        ContentDialog dialog;
        dialog.XamlRoot(this->Content().XamlRoot());
        dialog.Title(box_value(L"Atteindre une date"));
        dialog.PrimaryButtonText(L"Atteindre");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(12);
        panel.MinWidth(320);

        TextBlock desc;
        desc.Text(L"Sélectionnez une date pour naviguer directement dans le calendrier :");
        desc.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        panel.Children().Append(desc);

        CalendarDatePicker date_picker;
        date_picker.HorizontalAlignment(HorizontalAlignment::Stretch);
        time_t anchor = m_selected_day_start > 0 ? static_cast<time_t>(m_selected_day_start) : std::time(nullptr);
        date_picker.Date(winrt::clock::from_time_t(anchor));
        panel.Children().Append(date_picker);

        // Raccourcis rapides : Aujourd'hui, 7 prochains jours
        StackPanel shortcuts;
        shortcuts.Orientation(Orientation::Horizontal);
        shortcuts.Spacing(8);

        Button today_btn;
        today_btn.Content(box_value(L"Aujourd'hui"));
        today_btn.Click([date_picker](IInspectable const&, RoutedEventArgs const&)
        {
            date_picker.Date(winrt::clock::now());
        });
        shortcuts.Children().Append(today_btn);

        Button next_week_btn;
        next_week_btn.Content(box_value(L"+7 jours"));
        next_week_btn.Click([date_picker](IInspectable const&, RoutedEventArgs const&)
        {
            if (date_picker.Date())
            {
                time_t cur = winrt::clock::to_time_t(date_picker.Date().Value());
                date_picker.Date(winrt::clock::from_time_t(cur + 7 * 86400));
            }
        });
        shortcuts.Children().Append(next_week_btn);

        panel.Children().Append(shortcuts);
        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        if (result == ContentDialogResult::Primary && date_picker.Date())
        {
            if (m_view_tag != L"calendar")
            {
                SelectNavigationModule(L"calendar");
            }
            GoToCalendarDate(date_picker.Date().Value());
        }
    }

    /* Période affichée (jour / semaine / mois), mémorisée ; synchronise la
     * barre de navigation. */
    void MainWindow::SetCalendarPeriod(hstring const& period)
    {
        m_calendar_period = period == L"day" || period == L"workweek" || period == L"month" || period == L"list"
            ? period : hstring{ L"week" };
        ::EvolutionWin::Settings::Instance().SetString(L"calendar.period", m_calendar_period);

        m_updating_period = true;
        for (auto const& item : CalendarPeriodBar().Items())
            item.IsSelected(unbox_value_or<hstring>(item.Tag(), L"") == m_calendar_period);
        m_updating_period = false;

        LoadEvents();
    }

    void MainWindow::OnCalendarPeriodClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        SetCalendarPeriod(unbox_value_or<hstring>(sender.as<FrameworkElement>().Tag(), L"week"));
    }

    void MainWindow::OnCalendarPeriodChanged(SelectorBar const& sender, SelectorBarSelectionChangedEventArgs const&)
    {
        if (m_updating_period || !sender.SelectedItem())
            return;
        SetCalendarPeriod(unbox_value_or<hstring>(sender.SelectedItem().Tag(), L"week"));
    }

    /* Période précédente (-1) ou suivante (+1). */
    void MainWindow::ShiftCalendarPeriod(int direction)
    {
        time_t anchor = m_selected_day_start > 0 ? static_cast<time_t>(m_selected_day_start) : std::time(nullptr);
        struct tm local_tm{};
        localtime_s(&local_tm, &anchor);
        local_tm.tm_hour = 0;
        local_tm.tm_min = 0;
        local_tm.tm_sec = 0;
        local_tm.tm_isdst = -1;

        if (m_calendar_period == L"month")
        {
            local_tm.tm_mday = 1;
            local_tm.tm_mon += direction;
        }
        else
        {
            local_tm.tm_mday += direction * (m_calendar_period == L"day" ? 1 : m_calendar_period == L"list" ? 30 : 7);
        }

        m_selected_day_start = static_cast<int64_t>(mktime(&local_tm));
        MonthCalendar().SetDisplayDate(winrt::clock::from_time_t(static_cast<time_t>(m_selected_day_start)));
        LoadEvents();
    }

    void MainWindow::OnCalendarPreviousClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShiftCalendarPeriod(-1);
    }

    void MainWindow::OnCalendarNextClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShiftCalendarPeriod(+1);
    }

    /* -------------------------------------------------------------------- */
    /* Étape 3 : Calendrier (ECalClient) et Contacts (EBookClient)          */
    /* -------------------------------------------------------------------- */

    /* Charge et affiche les événements des calendriers EDS autour de la date
     * sélectionnée (ou les 30 prochains jours par défaut). */
    void MainWindow::LoadEvents()
    {
        /* plage de la période autour du jour de référence (aujourd'hui par
           défaut) : jour, semaine du lundi au dimanche, mois civil */
        time_t anchor = m_selected_day_start > 0 ? static_cast<time_t>(m_selected_day_start) : std::time(nullptr);
        struct tm first{};
        localtime_s(&first, &anchor);
        first.tm_hour = 0;
        first.tm_min = 0;
        first.tm_sec = 0;
        first.tm_isdst = -1;

        /* jours affichés : 1 (Jour), lundi-vendredi, lundi-dimanche, six
           semaines autour du mois, 30 jours (Liste) */
        int days = 1;
        struct tm month_first = first;
        month_first.tm_mday = 1;
        int64_t month_unix = static_cast<int64_t>(mktime(&month_first));
        m_grid_month = month_first.tm_mon;

        int first_day = static_cast<int>(::EvolutionWin::Settings::Instance().GetNumber(L"calendar.first-day", 0));
        auto get_day_offset = [first_day](int tm_wday) {
            if (first_day == 1) // Dimanche
                return tm_wday;
            if (first_day == 2) // Samedi
                return (tm_wday + 1) % 7;
            return (tm_wday + 6) % 7; // Lundi par défaut
        };

        if (m_calendar_period == L"month")
        {
            first = month_first;
            first.tm_mday -= get_day_offset(first.tm_wday);
            days = 42;
        }
        else if (m_calendar_period == L"week" || m_calendar_period == L"workweek")
        {
            first.tm_mday -= get_day_offset(first.tm_wday);
            days = m_calendar_period == L"week" ? 7 : 5;
        }
        else if (m_calendar_period == L"list")
        {
            days = 30;
        }

        struct tm last = first;
        last.tm_mday += days;
        last.tm_isdst = -1;
        first.tm_isdst = -1;
        int64_t start_unix = static_cast<int64_t>(mktime(&first));
        int64_t end_unix = static_cast<int64_t>(mktime(&last));
        m_grid_start = start_unix;
        m_grid_days = days;

        /* plage affichée : « samedi 31 octobre 2026 », « 26 oct. – 1 nov. 2026 »,
           « octobre 2026 » */
        {
            using Windows::Globalization::DateTimeFormatting::DateTimeFormatter;
            auto from = winrt::clock::from_time_t(static_cast<time_t>(start_unix));
            auto to = winrt::clock::from_time_t(static_cast<time_t>(end_unix - 1));
            hstring label;

            if (m_calendar_period == L"month")
                label = DateTimeFormatter{ L"month.full year.full" }.Format(winrt::clock::from_time_t(static_cast<time_t>(month_unix)));
            else if (days > 1)
                label = DateTimeFormatter{ L"day month.abbreviated" }.Format(from) + L" – " +
                        DateTimeFormatter{ L"day month.abbreviated year.full" }.Format(to);
            else
                label = DateTimeFormatter{ L"dayofweek.full day month.full year.full" }.Format(from);

            CalendarHeader().Text(label);
        }

        bool list_view = m_calendar_period == L"list";
        EventList().Visibility(list_view ? Visibility::Visible : Visibility::Collapsed);
        CalendarGridHost().Visibility(list_view ? Visibility::Collapsed : Visibility::Visible);

        if (!m_calendar_sources)
        {
            LoadCalendarSources();
            return;
        }

        std::wstring visible;
        std::vector<hstring> person_uids;   /* calendriers de personnes (Graph, lecture seule) */
        for (auto const& uid : VisibleCalendarUids())
        {
            if (::EvolutionWin::People::IsPersonUid(std::wstring_view{ uid }))
                person_uids.push_back(uid);
            else
                visible += (visible.empty() ? L"" : L",") + std::wstring{ uid };
        }

        if (visible.empty() && person_uids.empty())
        {
            EventList().Items().Clear();
            TextBlock none;
            none.Text(L"Aucun calendrier affiché : cochez un calendrier dans la liste de gauche.");
            none.Padding(ThicknessHelper::FromLengths(12, 12, 12, 12));
            EventList().Items().Append(none);
            return;
        }

        /* événements des calendriers de personnes, fusionnés avec ceux des calendriers EDS */
        auto people_events = std::make_shared<JsonArray>();
        auto pending = std::make_shared<int>(visible.empty() ? 0 : 1);
        auto people_warned = std::make_shared<bool>(false);
        auto finish = [this, people_events, pending](hstring const& json)
        {
            JsonArray occurrences;
            JsonArray::TryParse(json, occurrences);
            for (auto const& value : *people_events)
                occurrences.Append(value);
            m_all_calendar_events = occurrences;
            FilterAndRenderCalendarEvents();
        };

        auto edsJson = std::make_shared<hstring>(L"[]");
        auto person_done = [pending, finish, edsJson]()
        {
            if (--*pending == 0)
                finish(*edsJson);
        };

        *pending += static_cast<int>(person_uids.size());
        for (auto const& uid : person_uids)
        {
            hstring email = ::EvolutionWin::People::EmailFromUid(uid);
            hstring name = email, color, account_uid;
            for (auto const& value : ::EvolutionWin::People::LoadAll())
            {
                auto person = value.GetObject();
                if (person.GetNamedString(L"email", L"") == email)
                {
                    name = person.GetNamedString(L"name", email);
                    color = person.GetNamedString(L"color", L"");
                    account_uid = person.GetNamedString(L"account_uid", L"");
                }
            }
            m_core->ListPersonEvents(account_uid, email, name, color, start_unix, end_unix,
                [this, people_events, people_warned, person_done](hstring const& json, hstring const& error)
            {
                JsonArray events;
                if (error.empty() && JsonArray::TryParse(json, events))
                {
                    for (auto const& value : events)
                        people_events->Append(value);
                }
                else if (!error.empty() && !*people_warned)
                {
                    *people_warned = true;   /* un seul message par chargement */
                    ShowStatus(error, InfoBarSeverity::Warning);
                }
                person_done();
            });
        }

        if (visible.empty())
            return;   /* seuls des calendriers de personnes sont affichés : person_done () conclut */

        m_core->ListCalendarEvents(start_unix, end_unix, hstring{ visible }, [this, edsJson, person_done](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Calendrier : " + error, InfoBarSeverity::Error);
                UpdateConnectionStatus(m_account.empty() ? L"global" : m_account, L"error", L"Anomalie de synchronisation du calendrier : " + error);
                person_done();
                return;
            }

            // Rétablissement du statut si l'anomalie calendrier précédente est résolue
            if (!m_is_offline)
            {
                auto it = m_account_states.find(std::wstring{ m_account.empty() ? L"global" : m_account });
                if (it != m_account_states.end() && it->second.status == L"error" && std::wstring_view(it->second.message).find(L"calendrier") != std::wstring_view::npos)
                {
                    UpdateConnectionStatus(m_account.empty() ? L"global" : m_account, L"connected", L"Synchronisé");
                }
            }

            *edsJson = json;
            person_done();
        });
    }

    /**
     * @brief Filtre localement les événements de calendrier chargés selon la requête textuelle et met à jour l'affichage.
     */
    void MainWindow::FilterAndRenderCalendarEvents()
    {
        if (!m_all_calendar_events)
            return;

        std::wstring query{ m_calendar_search_query };

        JsonArray filtered_events;
        for (auto const& value : m_all_calendar_events)
        {
            auto ev = value.GetObject();
            hstring summary = ev.GetNamedString(L"summary", L"");
            hstring location = ev.GetNamedString(L"location", L"");
            hstring description = ev.GetNamedString(L"description", L"");
            if (::EvolutionWin::Categories::MatchesCategoryFilter(::EvolutionWin::CategoryStore::CategoriesOf(ev), m_event_category_filter) &&
                MatchCalendarEvent(summary, location, description, query))
            {
                filtered_events.Append(value);
            }
        }

        if (m_calendar_period != L"list")
        {
            if (m_calendar_period == L"month")
                RenderMonthGrid(filtered_events, m_grid_start, m_grid_month);
            else
                RenderCalendarGrid(filtered_events, m_grid_start, m_grid_days);
            return;
        }

        auto items = EventList().Items();
        items.Clear();

        for (auto const& value : filtered_events)
        {
            auto ev = value.GetObject();
            hstring uid = ev.GetNamedString(L"uid", L"");
            hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
            hstring summary = ev.GetNamedString(L"summary", L"(Sans titre)");
            hstring location = ev.GetNamedString(L"location", L"");
            hstring description = ev.GetNamedString(L"description", L"");
            double start_t = ev.GetNamedNumber(L"start", 0);
            double end_t = ev.GetNamedNumber(L"end", 0);

            Grid row;
            if (m_compact_density)
                row.Padding(ThicknessHelper::FromLengths(10, 3, 10, 3));
            else
                row.Padding(ThicknessHelper::FromLengths(12, 10, 12, 10));
            row.ColumnSpacing(12);
            ColumnDefinition color_col;
            color_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(color_col);
            row.ColumnDefinitions().Append(ColumnDefinition{});
            ColumnDefinition del_col;
            del_col.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(del_col);

            /* couleur de la première catégorie, sinon couleur du calendrier (EDS), sinon couleur d'accent */
            Border color_bar;
            color_bar.Width(4);
            color_bar.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
            auto event_categories = ::EvolutionWin::CategoryStore::CategoriesOf(ev);
            std::wstring color{ ::EvolutionWin::CategoryStore::EventColorHex(ev) };
            if (color.size() == 7 && color[0] == L'#')
            {
                auto channel = [&](size_t at) { return static_cast<uint8_t>(std::wcstoul(color.substr(at, 2).c_str(), nullptr, 16)); };
                color_bar.Background(SolidColorBrush{ Microsoft::UI::ColorHelper::FromArgb(255, channel(1), channel(3), channel(5)) });
            }
            else
            {
                color_bar.Background(Application::Current().Resources().Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Brush>());
            }
            ToolTipService::SetToolTip(color_bar, box_value(ev.GetNamedString(L"calendar_name", L"")));
            row.Children().Append(color_bar);

            StackPanel details;
            details.Spacing(2);

            TextBlock title;
            title.Text(summary);
            title.Style(Application::Current().Resources().Lookup(box_value(L"BodyStrongTextBlockStyle")).as<Style>());
            details.Children().Append(title);

            hstring time_str = FormatDate(start_t) + (end_t > start_t ? L" → " + FormatDate(end_t) : hstring{});
            if (!location.empty())
                time_str = time_str + L"  •  Lieu : " + location;

            TextBlock sub;
            sub.Text(time_str);
            sub.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            details.Children().Append(sub);

            if (!description.empty())
            {
                TextBlock desc;
                desc.Text(description);
                desc.TextWrapping(TextWrapping::Wrap);
                details.Children().Append(desc);
            }

            if (!event_categories.empty())
                details.Children().Append(BuildCategoryChips(event_categories));

            Grid::SetColumn(details, 1);
            row.Children().Append(details);

            AttachCategoryContextMenu(row, event_categories, [this, ev](std::vector<std::wstring> const& names)
            {
                SetEventCategories(ev, names);
            });

            Button del_btn;
            FontIcon del_icon;
            del_icon.Glyph(L"\xE74D");
            del_icon.FontSize(14);
            del_btn.Content(del_icon);
            del_btn.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(del_btn, 2);
            ToolTipService::SetToolTip(del_btn, box_value(L"Supprimer l'événement"));
            del_btn.Click([this, calendar_uid, uid](IInspectable const&, RoutedEventArgs const&)
            {
                m_core->DeleteEvent(calendar_uid, uid, [this](hstring const&, hstring const& err)
                {
                    if (!err.empty())
                    {
                        ShowStatus(L"Suppression de l'événement : " + err, InfoBarSeverity::Error);
                        return;
                    }
                    LoadEvents();
                });
            });
            row.Children().Append(del_btn);

            /* ouverture au clic (OnEventItemClick) : "<agenda>\x1F<événement>" */
            row.Tag(box_value(calendar_uid + L"\x1F" + uid));
            items.Append(row);
        }

        if (filtered_events.Size() == 0)
        {
            TextBlock empty;
            if (!query.empty())
                empty.Text(L"Aucun événement ne correspond à « " + hstring{ query } + L" ».");
            else
                empty.Text(L"Aucun événement sur cette période.");
            empty.Padding(ThicknessHelper::FromLengths(12, 12, 12, 12));
            items.Append(empty);
        }
    }

    /**
     * @brief Gestionnaire de modification du texte du champ de recherche de calendrier.
     * @param sender Champ AutoSuggestBox émetteur.
     * @param args Arguments d'événement de changement de texte.
     */
    void MainWindow::OnCalendarSearchTextChanged(AutoSuggestBox const& sender, AutoSuggestBoxTextChangedEventArgs const& args)
    {
        if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput)
            return;

        m_calendar_search_query = sender.Text();
        if (SearchBox() && SearchBox().Text() != sender.Text())
            SearchBox().Text(sender.Text());
        FilterAndRenderCalendarEvents();
    }

    /**
     * @brief Gestionnaire de validation de la recherche dans le calendrier.
     * @param sender Champ AutoSuggestBox émetteur.
     * @param args Arguments de validation contenant la requête saisie.
     */
    void MainWindow::OnCalendarSearchQuerySubmitted(AutoSuggestBox const&, AutoSuggestBoxQuerySubmittedEventArgs const& args)
    {
        hstring query = args ? args.QueryText() : (CalendarSearchBox() ? CalendarSearchBox().Text() : hstring{});
        m_calendar_search_query = query;
        if (SearchBox() && SearchBox().Text() != query)
            SearchBox().Text(query);
        FilterAndRenderCalendarEvents();
    }

    /* Gestionnaire de sélection d'une date sur le calendrier mensuel. */
    void MainWindow::OnCalendarDateSelected(CalendarView const& sender, CalendarViewSelectedDatesChangedEventArgs const&)
    {
        auto dates = sender.SelectedDates();
        if (dates.Size() == 0)
        {
            m_selected_day_start = 0;
        }
        else
        {
            time_t raw = winrt::clock::to_time_t(dates.GetAt(0));
            struct tm local_tm{};
            localtime_s(&local_tm, &raw);
            local_tm.tm_hour = 0;
            local_tm.tm_min = 0;
            local_tm.tm_sec = 0;
            m_selected_day_start = static_cast<int64_t>(mktime(&local_tm));
        }
        LoadEvents();
    }

    /* Gestionnaire du bouton d'actualisation du calendrier. */
    void MainWindow::OnRefreshCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        /* affichage immédiat du cache local, puis actualisation des agendas distants */
        LoadEvents();
        if (!m_core)
            return;
        ShowStatus(L"Actualisation des agendas distants…", InfoBarSeverity::Informational);
        m_core->RefreshCalendars([this](hstring const&, hstring const&)
        {
            LoadEvents();
            ShowStatus(L"Agendas actualisés.", InfoBarSeverity::Success);
        });
    }

    /* Gestionnaire du bouton « Nouvel événement ». */
    void MainWindow::OnNewEventClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewEventDialogAsync();
    }

    /**
     * @brief Calendrier Microsoft 365 qui recevra une réunion Teams : le calendrier actif s'il est
     *        Microsoft 365, sinon le calendrier par défaut (ou le premier) d'un compte Microsoft 365.
     * @return Identifiant du calendrier ("" si aucun compte Microsoft 365 n'a de calendrier).
     */
    hstring MainWindow::TeamsCalendarUid()
    {
        hstring active = ActiveCalendarUid();
        hstring fallback;
        for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"backend", L"") != L"microsoft365")
                continue;
            hstring uid = source.GetNamedString(L"uid", L"");
            if (uid == active)
                return uid;
            if (fallback.empty() || source.GetNamedBoolean(L"default", false) || source.GetNamedString(L"name", L"") == L"Calendrier")
                fallback = uid;
        }
        return fallback;
    }

    /**
     * @brief Ouvre la fenêtre de nouvel événement sur un créneau sélectionné dans la grille du calendrier.
     * @param start_unix Début du créneau (secondes Unix).
     * @param end_unix Fin du créneau (secondes Unix).
     */
    void MainWindow::BeginEventFromSelection(int64_t start_unix, int64_t end_unix)
    {
        m_new_event_start = start_unix;
        m_new_event_end = end_unix;
        ShowEventDialogAsync(nullptr);
    }

    /* Nouvel événement : fenêtre d'événement vide (calendrier actif). */
    fire_and_forget MainWindow::ShowNewEventDialogAsync()
    {
        ShowEventDialogAsync(nullptr);
        co_return;
    }

    /**
     * @brief Gestionnaire de création d'un temps de travail individuel (Focus).
     * @param sender Bouton émetteur.
     * @param args Arguments d'événement.
     */
    void MainWindow::OnNewFocusEventClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowNewFocusEventDialogAsync();
    }

    fire_and_forget MainWindow::ShowNewFocusEventDialogAsync()
    {
        ShowEventDialogAsync(nullptr, L"Temps de concentration", true);
        co_return;
    }

    /* Clic sur un événement de la liste : détail lu par evo-core, puis
     * fenêtre de l'événement. */
    void MainWindow::OnEventItemClick(IInspectable const&, ItemClickEventArgs const& args)
    {
        auto row = args.ClickedItem().try_as<FrameworkElement>();
        hstring key = row ? unbox_value_or<hstring>(row.Tag(), L"") : hstring{};
        std::wstring text{ key };
        auto sep = text.find(L'\x1F');
        if (sep == std::wstring::npos)
            return;

        hstring calendar_uid{ text.substr(0, sep) };
        hstring uid{ text.substr(sep + 1) };

        OpenCalendarEvent(calendar_uid, uid);
    }

    /* Réponse d'un participant en clair. */
    static hstring AttendeeStatusLabel(hstring const& status)
    {
        if (status == L"accepted") return L"accepté";
        if (status == L"declined") return L"refusé";
        if (status == L"tentative") return L"provisoire";
        if (status == L"delegated") return L"délégué";
        return L"sans réponse";
    }

    /* Fenêtre d'un événement : nouveau (event nul, calendrier actif) ou
     * existant (détail evo-core : modifiable sauf agenda en lecture seule,
     * organisateur et participants, suppression).
     * Paramètres :
     *   event : détail renvoyé par evo_core_get_event (), ou nullptr */
    fire_and_forget MainWindow::ShowEventDialogAsync(JsonObject event, hstring const& summary_preset, bool is_focus, bool teams)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        bool existing = static_cast<bool>(event);
        bool readonly = existing && event.GetNamedBoolean(L"readonly", false);
        bool all_day = existing && event.GetNamedBoolean(L"all_day", false);

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(existing ? (readonly ? L"Événement (lecture seule)" : L"Événement") : (is_focus ? L"Temps individuel (Focus)" : L"Nouvel événement")));
        if (!readonly)
            dialog.PrimaryButtonText(L"Enregistrer");
        if (existing && !readonly)
            dialog.SecondaryButtonText(L"Supprimer");
        dialog.CloseButtonText(existing ? L"Fermer" : L"Annuler");
        dialog.DefaultButton(readonly ? ContentDialogButton::Close : ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(420);

        auto caption = [](hstring const& text)
        {
            TextBlock block;
            block.Text(text);
            block.TextWrapping(TextWrapping::Wrap);
            block.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            block.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
            return block;
        };

        if (existing)
        {
            panel.Children().Append(caption(L"Calendrier : " + event.GetNamedString(L"calendar_name", L"")));
            if (event.GetNamedBoolean(L"recurring", false))
                panel.Children().Append(caption(L"Événement périodique : la modification s'applique à toute la série."));

            Button create_task_btn;
            StackPanel btn_content;
            btn_content.Orientation(Orientation::Horizontal);
            btn_content.Spacing(6);
            FontIcon task_btn_icon;
            task_btn_icon.Glyph(L"\xE9D5");
            task_btn_icon.FontSize(13);
            btn_content.Children().Append(task_btn_icon);
            TextBlock btn_label;
            btn_label.Text(L"Créer une tâche liée à cet événement");
            btn_content.Children().Append(btn_label);
            create_task_btn.Content(btn_content);
            create_task_btn.HorizontalAlignment(HorizontalAlignment::Left);
            create_task_btn.Margin(ThicknessHelper::FromLengths(0, 2, 0, 4));
            create_task_btn.Click([this, event, &dialog](IInspectable const&, RoutedEventArgs const&)
            {
                dialog.Hide();
                CreateTaskFromEvent(event);
            });
            panel.Children().Append(create_task_btn);
        }

        TextBox summary_box;
        summary_box.Header(box_value(L"Titre"));
        summary_box.PlaceholderText(L"Réunion, Rendez-vous…");
        summary_box.IsReadOnly(readonly);
        if (existing)
            summary_box.Text(event.GetNamedString(L"summary", L""));
        else if (!summary_preset.empty())
            summary_box.Text(summary_preset);
        panel.Children().Append(summary_box);

        TextBox location_box;
        location_box.Header(box_value(L"Lieu"));
        location_box.IsReadOnly(readonly);
        if (existing)
            location_box.Text(event.GetNamedString(L"location", L""));
        panel.Children().Append(location_box);

        /* début / fin : date et heure */
        /* créneau sélectionné dans la grille du calendrier : utilisé une seule fois */
        bool from_selection = !existing && m_new_event_start > 0 && m_new_event_end > m_new_event_start;
        time_t start_t = existing ? static_cast<time_t>(event.GetNamedNumber(L"start", 0))
            : static_cast<time_t>(from_selection ? m_new_event_start
                : (m_selected_day_start > 0 ? m_selected_day_start + 9 * 3600 : 0));
        time_t end_t = existing ? static_cast<time_t>(event.GetNamedNumber(L"end", 0))
            : (from_selection ? static_cast<time_t>(m_new_event_end) : start_t + (is_focus ? 7200 : 3600));
        if (!existing)
        {
            m_new_event_start = 0;
            m_new_event_end = 0;
        }
        if (!existing && start_t == 0)
        {
            time_t now = std::time(nullptr);
            struct tm next{};
            localtime_s(&next, &now);
            next.tm_hour = 9;
            next.tm_min = 0;
            next.tm_sec = 0;
            start_t = mktime(&next);
            end_t = start_t + 3600;
        }
        /* événement daté dans un fuseau (TZID) : les sélecteurs affichent
         * l'heure murale de ce fuseau « AAAAMMJJTHHMMSS » */
        hstring event_tz = existing ? event.GetNamedString(L"timezone", L"") : hstring{};
        auto wall_to_time = [](hstring const& wall, time_t fallback) -> time_t
        {
            int y = 0, mo = 0, d = 0, h = 0, mi = 0;
            if (swscanf_s(wall.c_str(), L"%4d%2d%2dT%2d%2d", &y, &mo, &d, &h, &mi) != 5)
                return fallback;
            struct tm local{};
            local.tm_year = y - 1900;
            local.tm_mon = mo - 1;
            local.tm_mday = d;
            local.tm_hour = h;
            local.tm_min = mi;
            local.tm_isdst = -1;
            return mktime(&local);
        };
        if (existing && !event_tz.empty() && !all_day)
        {
            start_t = wall_to_time(event.GetNamedString(L"start_wall", L""), start_t);
            end_t = wall_to_time(event.GetNamedString(L"end_wall", L""), end_t);
        }
        /* journée entière : la fin (exclusive) est le lendemain */
        if (all_day && end_t > start_t)
            end_t -= 86400;

        auto time_of_day = [](time_t t)
        {
            struct tm local{};
            localtime_s(&local, &t);
            return std::chrono::hours(local.tm_hour) + std::chrono::minutes(local.tm_min);
        };

        CheckBox all_day_box;
        all_day_box.Content(box_value(L"Journée entière"));
        all_day_box.IsChecked(all_day);
        all_day_box.IsEnabled(!readonly);
        panel.Children().Append(all_day_box);

        Grid start_row;
        start_row.ColumnSpacing(8);
        start_row.ColumnDefinitions().Append(ColumnDefinition{});
        start_row.ColumnDefinitions().Append(ColumnDefinition{});
        CalendarDatePicker start_date;
        start_date.Header(box_value(L"Début"));
        start_date.Date(winrt::clock::from_time_t(start_t));
        start_date.IsEnabled(!readonly);
        TimePicker start_time;
        start_time.Header(box_value(L" "));
        start_time.Time(time_of_day(start_t));
        start_time.IsEnabled(!readonly);
        Grid::SetColumn(start_time, 1);
        start_row.Children().Append(start_date);
        start_row.Children().Append(start_time);
        panel.Children().Append(start_row);

        Grid end_row;
        end_row.ColumnSpacing(8);
        end_row.ColumnDefinitions().Append(ColumnDefinition{});
        end_row.ColumnDefinitions().Append(ColumnDefinition{});
        CalendarDatePicker end_date;
        end_date.Header(box_value(L"Fin"));
        end_date.Date(winrt::clock::from_time_t(end_t));
        end_date.IsEnabled(!readonly);
        TimePicker end_time;
        end_time.Header(box_value(L" "));
        end_time.Time(time_of_day(end_t));
        end_time.IsEnabled(!readonly);
        Grid::SetColumn(end_time, 1);
        end_row.Children().Append(end_date);
        end_row.Children().Append(end_time);
        panel.Children().Append(end_row);

        /* rappel (VALARM) : délai avant le début */
        static constexpr int reminder_values[] = { -1, 0, 5, 15, 30, 60, 120, 1440 };
        ComboBox reminder_combo;
        reminder_combo.Header(box_value(L"Rappel"));
        reminder_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        reminder_combo.IsEnabled(!readonly);
        for (hstring label : { L"Aucun", L"À l'heure de l'événement", L"5 minutes avant", L"15 minutes avant",
                               L"30 minutes avant", L"1 heure avant", L"2 heures avant", L"1 jour avant" })
            reminder_combo.Items().Append(box_value(label));
        {
            int current = existing ? static_cast<int>(event.GetNamedNumber(L"reminder_minutes", -1)) : 15;
            int selected = 0;
            for (int i = 0; i < 8; ++i)
                if (reminder_values[i] == current)
                    selected = i;
            reminder_combo.SelectedIndex(selected);
        }
        panel.Children().Append(reminder_combo);

        /* fuseau horaire (noms IANA) : le premier choix conserve l'heure
         * de cet ordinateur, enregistrée en UTC */
        std::vector<hstring> zone_ids{ L"", L"Europe/Zurich", L"Europe/Paris", L"Europe/London", L"Europe/Berlin",
            L"Europe/Madrid", L"Europe/Rome", L"America/New_York", L"America/Chicago", L"America/Denver",
            L"America/Los_Angeles", L"America/Sao_Paulo", L"Asia/Dubai", L"Asia/Kolkata", L"Asia/Shanghai",
            L"Asia/Tokyo", L"Australia/Sydney", L"UTC" };
        if (!event_tz.empty() && std::find(zone_ids.begin(), zone_ids.end(), event_tz) == zone_ids.end())
            zone_ids.push_back(event_tz);
        ComboBox zone_combo;
        zone_combo.Header(box_value(L"Fuseau horaire"));
        zone_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        zone_combo.IsEnabled(!readonly);
        for (auto const& zone : zone_ids)
            zone_combo.Items().Append(box_value(zone.empty() ? hstring{ L"Heure de cet ordinateur" } : zone));
        {
            auto found = std::find(zone_ids.begin(), zone_ids.end(), event_tz);
            zone_combo.SelectedIndex(static_cast<int>(found - zone_ids.begin()));
        }
        panel.Children().Append(zone_combo);

        auto update_times = [start_time, end_time, zone_combo](bool day)
        {
            start_time.Visibility(day ? Visibility::Collapsed : Visibility::Visible);
            end_time.Visibility(day ? Visibility::Collapsed : Visibility::Visible);
            zone_combo.Visibility(day ? Visibility::Collapsed : Visibility::Visible);
        };
        update_times(all_day);
        all_day_box.Click([all_day_box, update_times](IInspectable const&, RoutedEventArgs const&)
        {
            update_times(all_day_box.IsChecked() && all_day_box.IsChecked().GetBoolean());
        });

        /* participants modifiables : adresses séparées par des points-virgules */
        TextBox attendees_box;
        attendees_box.Header(box_value(L"Participants (adresses séparées par « ; »)"));
        attendees_box.IsReadOnly(readonly);
        attendees_box.PlaceholderText(L"alice@exemple.ch; bob@exemple.ch");
        if (existing)
        {
            std::wstring joined;
            for (auto const& value : event.GetNamedArray(L"attendees", JsonArray{}))
            {
                hstring email = value.GetObject().GetNamedString(L"email", L"");
                if (email.empty())
                    continue;
                joined += (joined.empty() ? L"" : L"; ");
                joined += email.c_str();
            }
            attendees_box.Text(hstring{ joined });
        }
        panel.Children().Append(attendees_box);

        /* réunion en ligne Microsoft Teams (nouvel événement d'un calendrier Exchange Online) */
        CheckBox teams_check;
        teams_check.Content(box_value(Tr(L"Réunion en ligne Microsoft Teams (lien de visio ajouté à l'invitation)",
                                         L"Microsoft Teams online meeting (join link added to the invitation)")));
        teams_check.IsChecked(teams);
        if (!existing && !readonly && !TeamsCalendarUid().empty())
            panel.Children().Append(teams_check);

        /* invitations : compte de messagerie expéditeur (adresse de l'organisateur) */
        ComboBox sender_combo;
        sender_combo.Header(box_value(Tr(L"Envoyer les invitations depuis", L"Send invitations from")));
        sender_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        std::vector<hstring> sender_uids;
        {
            int selected = -1;
            for (auto const& value : m_accounts ? m_accounts : JsonArray{})
            {
                auto account = value.GetObject();
                hstring address = account.GetNamedString(L"address", L"");
                if (account.GetNamedBoolean(L"local", false) || address.empty())
                    continue;
                hstring uid = account.GetNamedString(L"uid", L"");
                hstring name = account.GetNamedString(L"name", address);
                sender_combo.Items().Append(box_value(name == address ? address : name + L" <" + address + L">"));
                if (selected < 0 && (uid == m_account || (m_account.empty() && account.GetNamedBoolean(L"default", false))))
                    selected = static_cast<int>(sender_uids.size());
                sender_uids.push_back(uid);
            }
            if (!sender_uids.empty())
                sender_combo.SelectedIndex(selected >= 0 ? selected : 0);
        }
        CheckBox invite_check;
        invite_check.Content(box_value(Tr(L"Envoyer les invitations aux participants", L"Send invitations to attendees")));
        invite_check.IsChecked(true);
        if (!readonly && !sender_uids.empty())
        {
            panel.Children().Append(sender_combo);
            panel.Children().Append(invite_check);
        }

        /* organisateur et participants (lecture seule) */
        if (existing)
        {
            auto person = [](JsonObject const& who)
            {
                hstring name = who.GetNamedString(L"name", L"");
                hstring email = who.GetNamedString(L"email", L"");
                return name.empty() ? email : (email.empty() ? name : name + L" <" + email + L">");
            };

            auto organizer = event.GetNamedObject(L"organizer", JsonObject{});
            if (!person(organizer).empty())
                panel.Children().Append(caption(L"Organisateur : " + person(organizer)));

            auto attendees = event.GetNamedArray(L"attendees", JsonArray{});
            if (attendees.Size() > 0)
            {
                std::wstring list = L"Participants :";
                for (auto const& value : attendees)
                {
                    auto who = value.GetObject();
                    list += L"\n  • " + std::wstring{ person(who) } + L" — " +
                            std::wstring{ AttendeeStatusLabel(who.GetNamedString(L"status", L"")) };
                }
                panel.Children().Append(caption(hstring{ list }));
            }
        }

        TextBox desc_box;
        desc_box.Header(box_value(L"Description"));
        desc_box.AcceptsReturn(true);
        desc_box.TextWrapping(TextWrapping::Wrap);
        desc_box.Height(120);
        desc_box.IsReadOnly(readonly);
        if (existing)
            desc_box.Text(event.GetNamedString(L"description", L""));
        panel.Children().Append(desc_box);

        ScrollViewer scroll;
        scroll.Content(panel);
        scroll.MaxHeight(600);
        dialog.Content(scroll);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        if (existing && result == ContentDialogResult::Secondary)
        {
            m_core->DeleteEvent(event.GetNamedString(L"calendar_uid", L""), event.GetNamedString(L"uid", L""),
                [this](hstring const&, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Suppression de l'événement : " + error, InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(L"Événement supprimé.", InfoBarSeverity::Success);
                LoadEvents();
            });
            co_return;
        }

        if (readonly || result != ContentDialogResult::Primary || summary_box.Text().empty())
            co_return;

        /* date du sélecteur + heure du sélecteur (minuit en journée entière) */
        bool day = all_day_box.IsChecked() && all_day_box.IsChecked().GetBoolean();
        auto combine = [day](CalendarDatePicker const& date, TimePicker const& time) -> int64_t
        {
            time_t raw = winrt::clock::to_time_t(date.Date() ? date.Date().Value() : winrt::clock::now());
            struct tm local{};
            localtime_s(&local, &raw);
            local.tm_hour = 0;
            local.tm_min = 0;
            local.tm_sec = 0;
            local.tm_isdst = -1;
            int64_t base = static_cast<int64_t>(mktime(&local));
            return day ? base : base + std::chrono::duration_cast<std::chrono::seconds>(time.Time()).count();
        };

        int64_t start_unix = combine(start_date, start_time);
        int64_t end_unix = combine(end_date, end_time);
        if (day)
            end_unix += 86400;
        if (end_unix <= start_unix)
            end_unix = start_unix + (day ? 86400 : 3600);

        JsonObject payload;
        payload.SetNamedValue(L"summary", JsonValue::CreateStringValue(summary_box.Text()));
        payload.SetNamedValue(L"location", JsonValue::CreateStringValue(location_box.Text()));
        payload.SetNamedValue(L"description", JsonValue::CreateStringValue(desc_box.Text()));
        payload.SetNamedValue(L"start", JsonValue::CreateNumberValue(static_cast<double>(start_unix)));
        payload.SetNamedValue(L"end", JsonValue::CreateNumberValue(static_cast<double>(end_unix)));
        payload.SetNamedValue(L"all_day", JsonValue::CreateBooleanValue(day));
        {
            int sel = reminder_combo.SelectedIndex();
            payload.SetNamedValue(L"reminder_minutes",
                JsonValue::CreateNumberValue(sel >= 0 && sel < 8 ? reminder_values[sel] : -1));
        }

        /* fuseau choisi : heures murales envoyées au moteur */
        int zone_sel = zone_combo.SelectedIndex();
        if (!day && zone_sel > 0 && zone_sel < static_cast<int>(zone_ids.size()))
        {
            auto wall = [](int64_t t)
            {
                time_t raw = static_cast<time_t>(t);
                struct tm local{};
                localtime_s(&local, &raw);
                wchar_t text[32]{};
                swprintf_s(text, L"%04d%02d%02dT%02d%02d00", local.tm_year + 1900, local.tm_mon + 1,
                           local.tm_mday, local.tm_hour, local.tm_min);
                return hstring{ text };
            };
            payload.SetNamedValue(L"timezone", JsonValue::CreateStringValue(zone_ids[zone_sel]));
            payload.SetNamedValue(L"start_wall", JsonValue::CreateStringValue(wall(start_unix)));
            payload.SetNamedValue(L"end_wall", JsonValue::CreateStringValue(wall(end_unix)));
        }

        /* participants : adresses distinctes séparées par « ; », « , » ou espaces */
        {
            JsonArray list;
            std::wstring text{ attendees_box.Text() };
            std::vector<std::wstring> seen;
            size_t pos = 0;
            while (pos <= text.size())
            {
                size_t next = text.find_first_of(L";, \r\n\t", pos);
                if (next == std::wstring::npos)
                    next = text.size();
                std::wstring item = text.substr(pos, next - pos);
                if (item.find(L'@') != std::wstring::npos && std::find(seen.begin(), seen.end(), item) == seen.end())
                {
                    seen.push_back(item);
                    list.Append(JsonValue::CreateStringValue(item));
                }
                pos = next + 1;
            }
            payload.SetNamedValue(L"attendees", list);
        }

        /* compte expéditeur des invitations : vide si aucune invitation à envoyer */
        hstring sender_uid;
        if (invite_check.IsChecked() && invite_check.IsChecked().GetBoolean() && !readonly &&
            payload.GetNamedArray(L"attendees", JsonArray{}).Size() > 0 &&
            sender_combo.SelectedIndex() >= 0 && static_cast<size_t>(sender_combo.SelectedIndex()) < sender_uids.size())
            sender_uid = sender_uids[sender_combo.SelectedIndex()];

        if (existing)
        {
            payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(event.GetNamedString(L"calendar_uid", L"")));
            payload.SetNamedValue(L"uid", JsonValue::CreateStringValue(event.GetNamedString(L"uid", L"")));
            hstring edit_calendar = event.GetNamedString(L"calendar_uid", L"");
            hstring edit_uid = event.GetNamedString(L"uid", L"");
            m_core->ModifyEvent(payload.Stringify(), [this, sender_uid, edit_calendar, edit_uid](hstring const&, hstring const& error)
            {
                if (!error.empty())
                {
                    ShowStatus(L"Modification de l'événement : " + error, InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(L"Événement modifié.", InfoBarSeverity::Success);
                LoadEvents();
                SendEventInvitations(sender_uid, edit_calendar, edit_uid);
            });
            co_return;
        }

        if (hstring active = ActiveCalendarUid(); !active.empty())
            payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(active));

        /* réunion Teams : l'événement est créé dans un calendrier Microsoft 365 */
        if (teams_check.IsChecked() && teams_check.IsChecked().GetBoolean())
        {
            if (hstring teams_calendar = TeamsCalendarUid(); !teams_calendar.empty())
                payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(teams_calendar));
            payload.SetNamedValue(L"online_meeting", JsonValue::CreateBooleanValue(true));
        }

        hstring create_calendar = payload.GetNamedString(L"calendar_uid", L"");
        m_core->CreateEvent(payload.Stringify(), [this, sender_uid, create_calendar](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(L"Création de l'événement : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Événement enregistré.", InfoBarSeverity::Success);
            LoadEvents();
            JsonObject created;
            if (JsonObject::TryParse(json, created))
                SendEventInvitations(sender_uid, create_calendar, created.GetNamedString(L"uid", L""));
        });
    }

    /**
     * @brief Envoie les invitations d'un événement à ses participants lorsque le calendrier ne le
     *        fait pas lui-même (agenda local, abonnement) ; Exchange Online, Exchange et Google
     *        Agenda envoient leurs invitations côté serveur.
     * @param sender_uid Compte de messagerie expéditeur ("" : aucune invitation).
     * @param calendar_uid Agenda de l'événement.
     * @param event_uid Identifiant de l'événement enregistré.
     */
    void MainWindow::SendEventInvitations(hstring const& sender_uid, hstring const& calendar_uid, hstring const& event_uid)
    {
        if (sender_uid.empty() || calendar_uid.empty() || event_uid.empty())
            return;

        for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"uid", L"") != calendar_uid)
                continue;
            hstring backend = source.GetNamedString(L"backend", L"");
            if (backend == L"microsoft365" || backend == L"ews" || backend == L"google")
                return;   /* le serveur envoie les invitations */
        }

        m_core->SendEventInvitations(sender_uid, calendar_uid, event_uid, [this](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(Tr(L"Invitations non envoyées : ", L"Invitations not sent: ") + error, InfoBarSeverity::Error);
                return;
            }
            JsonObject result;
            if (JsonObject::TryParse(json, result) && result.GetNamedNumber(L"sent", 0) > 0)
                ShowStatus(Tr(L"Invitations envoyées à : ", L"Invitations sent to: ") + result.GetNamedString(L"to", L""),
                    InfoBarSeverity::Success);
        });
    }

    /* Gestionnaire du bouton « Exporter » du calendrier : liste les agendas et
     * listes de tâches, puis propose l'exportation de l'un d'eux. */
    void MainWindow::OnExportCalendarClick(IInspectable const&, RoutedEventArgs const&)
    {
        m_core->ListSources(L"calendar,tasks", [this](hstring const& json, hstring const& error)
        {
            JsonArray sources;
            if (!error.empty() || !JsonArray::TryParse(json, sources))
            {
                ShowStatus(L"Exportation : " + error, InfoBarSeverity::Error);
                return;
            }
            if (sources.Size() == 0)
            {
                ShowStatus(L"Aucun agenda ni liste de tâches à exporter.", InfoBarSeverity::Warning);
                return;
            }
            ExportCalendarAsync(sources);
        });
    }

    /* Demande l'agenda ou la liste de tâches à exporter, puis le fichier .ics
     * de destination, et lance l'exportation iCalendar par evo-core.
     * Paramètres :
     *   sources : tableau JSON [{"uid", "name", "kind"}] des agendas et listes de tâches */
    fire_and_forget MainWindow::ExportCalendarAsync(JsonArray sources)
    {
        auto strong = get_strong();
        if (m_dialog_open || !Content() || !Content().XamlRoot())
            co_return;

        m_dialog_open = true;

        ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(box_value(L"Exporter au format iCalendar"));
        dialog.PrimaryButtonText(L"Exporter");
        dialog.CloseButtonText(L"Annuler");
        dialog.DefaultButton(ContentDialogButton::Primary);

        StackPanel panel;
        panel.Spacing(10);
        panel.MinWidth(380);

        ComboBox source_box;
        source_box.Header(box_value(L"Agenda ou liste de tâches"));
        source_box.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (auto const& value : sources)
        {
            auto source = value.GetObject();
            hstring label = source.GetNamedString(L"kind", L"") == L"tasks" ? L"Tâches" : L"Agenda";
            source_box.Items().Append(box_value(label + L" : " + source.GetNamedString(L"name", L"")));
        }
        source_box.SelectedIndex(0);
        panel.Children().Append(source_box);

        TextBlock hint;
        hint.Text(L"Tous les événements ou toutes les tâches sont enregistrés dans un fichier .ics, avec leurs fuseaux horaires.");
        hint.TextWrapping(TextWrapping::Wrap);
        hint.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
        panel.Children().Append(hint);

        dialog.Content(panel);

        auto result = co_await dialog.ShowAsync();
        m_dialog_open = false;

        int index = source_box.SelectedIndex();
        if (result != ContentDialogResult::Primary || index < 0)
            co_return;

        auto source = sources.GetObjectAt(static_cast<uint32_t>(index));
        hstring uid = source.GetNamedString(L"uid", L"");
        hstring name = source.GetNamedString(L"name", L"");
        bool tasks = source.GetNamedString(L"kind", L"") == L"tasks";

        Windows::Storage::Pickers::FileSavePicker picker;
        InitializePicker(picker);
        picker.SuggestedFileName(SanitizeFileName(name, tasks ? L"taches" : L"agenda"));
        picker.FileTypeChoices().Insert(L"Calendrier iCalendar", single_threaded_vector<hstring>({ L".ics" }));

        auto file = co_await picker.PickSaveFileAsync();
        if (!file)
            co_return;
        hstring dest = file.Path();

        JsonObject request;
        request.SetNamedValue(L"source_uid", JsonValue::CreateStringValue(uid));
        request.SetNamedValue(L"path", JsonValue::CreateStringValue(dest));

        ShowStatus(L"Exportation de « " + name + L" »…", InfoBarSeverity::Informational);
        m_core->ExportCalendar(request.Stringify(), [this, dest, tasks](hstring const& json, hstring const& error)
        {
            JsonObject answer;
            if (!error.empty() || !JsonObject::TryParse(json, answer))
            {
                ShowStatus(L"Exportation : " + error, InfoBarSeverity::Error);
                return;
            }
            auto count = static_cast<uint32_t>(answer.GetNamedNumber(L"count", 0));
            ShowStatus(hstring{ std::to_wstring(count) + (tasks ? L" tâche(s) exportée(s)" : L" événement(s) exporté(s)") +
                L" dans « " + std::wstring{ dest } + L" »." }, InfoBarSeverity::Success);
        });
    }

    /**
     * @brief Gestionnaire de clic du bouton bascule du mini-calendrier dans le ruban.
     * @param sender Bouton bascule du ruban (AppBarToggleButton).
     * @param args Arguments d'evenement route.
     * Bascule l'affichage du mini-calendrier dans le volet lateral du calendrier,
     * memorise la preference dans les reglages et synchronise l'interrupteur des parametres.
     */
    void MainWindow::OnRibbonMiniCalendarToggled(IInspectable const& sender, RoutedEventArgs const&)
    {
        bool is_checked = unbox_value_or<bool>(sender.as<AppBarToggleButton>().IsChecked(), true);
        ::EvolutionWin::Settings::Instance().SetBoolean(L"calendar.show-mini-calendar", is_checked);
        if (auto cal = MonthCalendar())
            cal.Visibility(is_checked ? Visibility::Visible : Visibility::Collapsed);
        if (auto sw = CalendarShowMiniCalendarSwitch())
            sw.IsOn(is_checked);
    }
}
