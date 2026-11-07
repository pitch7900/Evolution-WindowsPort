#include "pch.h"
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
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
using namespace Microsoft::UI::Xaml::Controls::Primitives;
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

            AttachEventContextMenu(row, ev);

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
     * @brief Indique si un calendrier a déjà refusé une écriture (accès refusé par le serveur) : il est alors
     *        écarté de la liste des calendriers proposés pour un nouvel événement.
     * @param uid Identifiant du calendrier.
     * @return true si le calendrier est mémorisé en lecture seule.
     */
    static bool IsKnownReadOnlyCalendar(hstring const& uid)
    {
        for (auto const& entry : ::EvolutionWin::Settings::Instance().GetArray(L"calendar.readonly"))
            if (entry.ValueType() == JsonValueType::String && entry.GetString() == uid)
                return true;
        return false;
    }

    /**
     * @brief Mémorise qu'un calendrier refuse les écritures.
     * @param uid Identifiant du calendrier.
     */
    static void MarkReadOnlyCalendar(hstring const& uid)
    {
        if (uid.empty() || IsKnownReadOnlyCalendar(uid))
            return;
        auto& settings = ::EvolutionWin::Settings::Instance();
        JsonArray list = settings.GetArray(L"calendar.readonly");
        list.Append(JsonValue::CreateStringValue(uid));
        settings.Set(L"calendar.readonly", list);
    }

    /**
     * @brief Message d'erreur d'enregistrement d'un événement : si le serveur refuse l'accès, le calendrier est
     *        mémorisé en lecture seule et le message explique comment choisir un autre calendrier.
     * @param sources Calendriers connus (m_calendar_sources).
     * @param calendar_uid Calendrier visé.
     * @param action Début du message (« Création de l'événement : »).
     * @param error Erreur renvoyée par le moteur.
     * @return Texte à afficher.
     */
    static hstring EventSaveErrorText(JsonArray const& sources, hstring const& calendar_uid, hstring const& action, hstring const& error)
    {
        std::wstring text{ error };
        bool denied = text.find(L"AccessDenied") != std::wstring::npos || text.find(L"Access is denied") != std::wstring::npos ||
            text.find(L"ErrorAccessDenied") != std::wstring::npos;
        if (!denied)
            return action + error;

        MarkReadOnlyCalendar(calendar_uid);
        hstring name = calendar_uid;
        for (auto const& value : sources ? sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"uid", L"") == calendar_uid)
                name = source.GetNamedString(L"name", calendar_uid);
        }
        return Tr(L"Le calendrier « ", L"The calendar \"") + name +
            Tr(L" » est en lecture seule pour vous (accès refusé par le serveur). Choisissez un autre calendrier dans la liste « Calendrier » de la fenêtre d'événement.",
               L"\" is read-only for you (access denied by the server). Pick another calendar in the \"Calendar\" list of the event window.");
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
            if (IsKnownReadOnlyCalendar(uid))
                continue;
            if (uid == active)
                return uid;
            if (fallback.empty() || source.GetNamedBoolean(L"default", false) || source.GetNamedString(L"name", L"") == L"Calendrier")
                fallback = uid;
        }
        return fallback;
    }

    /**
     * @brief Calendrier Gmail qui recevra une réunion Google Meet : le calendrier actif s'il est
     *        Gmail, sinon le calendrier par défaut (ou le premier) d'un compte Gmail.
     * @return Identifiant du calendrier ("" si aucun compte Gmail n'a de calendrier).
     */
    hstring MainWindow::GoogleCalendarUid()
    {
        hstring active = ActiveCalendarUid();
        hstring fallback;
        for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
        {
            auto source = value.GetObject();
            if (source.GetNamedString(L"backend", L"") != L"google")
                continue;
            hstring uid = source.GetNamedString(L"uid", L"");
            if (IsKnownReadOnlyCalendar(uid))
                continue;
            if (uid == active)
                return uid;
            if (fallback.empty() || source.GetNamedBoolean(L"default", false))
                fallback = uid;
        }
        return fallback;
    }

    /**
     * @brief Contenu d'une case à cocher : icône SVG (dossier Assets à côté de l'exécutable) puis libellé.
     * @param check Case à cocher.
     * @param svg_name Nom du fichier SVG dans Assets.
     * @param label Libellé.
     */
    static void SetIconContent(ContentControl const& check, std::wstring const& svg_name, hstring const& label)
    {
        StackPanel content;
        content.Orientation(Orientation::Horizontal);
        content.Spacing(8);
        wchar_t exe_path[MAX_PATH]{};
        if (!svg_name.empty() && GetModuleFileNameW(nullptr, exe_path, MAX_PATH))
        {
            std::wstring svg_path{ exe_path };
            svg_path = svg_path.substr(0, svg_path.find_last_of(L"\\/")) + L"\\Assets\\" + svg_name;
            if (GetFileAttributesW(svg_path.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                Microsoft::UI::Xaml::Media::Imaging::SvgImageSource svg{ Windows::Foundation::Uri{ L"file:///" + hstring{ svg_path } } };
                Image icon;
                icon.Source(svg);
                icon.Width(20);
                icon.Height(20);
                icon.VerticalAlignment(VerticalAlignment::Center);
                content.Children().Append(icon);
            }
        }
        TextBlock text;
        text.Text(label);
        text.VerticalAlignment(VerticalAlignment::Center);
        content.Children().Append(text);
        check.Content(content);
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
    void MainWindow::ShowEventDialogAsync(JsonObject event, hstring const& summary_preset, bool is_focus, bool teams)
    {
        if (!Content())
            return;

        bool existing = static_cast<bool>(event);
        bool readonly = existing && event.GetNamedBoolean(L"readonly", false);
        bool all_day = existing && event.GetNamedBoolean(L"all_day", false);

        Window window;
        hstring window_title = existing ? (readonly ? L"Événement (lecture seule)" : L"Événement") : (is_focus ? L"Temps individuel (Focus)" : L"Nouvel événement");
        window.Title(window_title);
        window.AppWindow().Resize({ 1200, 860 });
        ::EvolutionWin::SetWindowIcon(window.AppWindow());
        window.SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
        window.ExtendsContentIntoTitleBar(true);

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

        /* calendrier de destination d'un nouvel événement (les calendriers refusant l'écriture sont écartés) */
        ComboBox calendar_combo;
        calendar_combo.Header(box_value(Tr(L"Calendrier", L"Calendar")));
        calendar_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        std::vector<hstring> calendar_uids;
        if (!existing && !readonly)
        {
            hstring active_uid = ActiveCalendarUid();
            int selected = -1, default_index = -1;
            for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
            {
                auto source = value.GetObject();
                hstring backend = source.GetNamedString(L"backend", L"");
                hstring uid = source.GetNamedString(L"uid", L"");
                if (backend == L"person" || backend == L"group" || source.GetNamedBoolean(L"placeholder", false) ||
                    uid.empty() || IsKnownReadOnlyCalendar(uid))
                    continue;
                hstring account = source.GetNamedString(L"account", L"");
                hstring group = source.GetNamedString(L"group_name", L"");
                std::wstring label{ source.GetNamedString(L"name", uid) };
                if (!account.empty())
                    label = std::wstring{ account } + L" \u203A " + (group.empty() ? L"" : std::wstring{ group } + L" \u203A ") + label;
                calendar_combo.Items().Append(box_value(hstring{ label }));
                if (uid == active_uid)
                    selected = static_cast<int>(calendar_uids.size());
                if (source.GetNamedBoolean(L"default", false))
                    default_index = static_cast<int>(calendar_uids.size());
                calendar_uids.push_back(uid);
            }
            if (!calendar_uids.empty())
            {
                calendar_combo.SelectedIndex(selected >= 0 ? selected : (default_index >= 0 ? default_index : 0));
                panel.Children().Append(calendar_combo);
            }
        }

        /* événement privé : détails masqués aux personnes avec qui le calendrier est partagé */
        ComboBox show_as_combo;
        show_as_combo.Header(box_value(Tr(L"Afficher comme", L"Show as")));
        show_as_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        /* mêmes états qu'Outlook (dans l'ordre de la liste d'Outlook) */
        static const wchar_t* const show_as_values[] = { L"free", L"working", L"tentative", L"busy", L"oof" };
        show_as_combo.Items().Append(box_value(Tr(L"Disponible", L"Free")));
        show_as_combo.Items().Append(box_value(Tr(L"Travaille ailleurs", L"Working elsewhere")));
        show_as_combo.Items().Append(box_value(Tr(L"Provisoire", L"Tentative")));
        show_as_combo.Items().Append(box_value(Tr(L"Occupé(e)", L"Busy")));
        show_as_combo.Items().Append(box_value(Tr(L"Absent(e)", L"Out of office")));
        {
            int show_as_index = 3;
            if (existing)
                for (int i = 0; i < 5; ++i)
                    if (event.GetNamedString(L"show_as", L"busy") == show_as_values[i])
                        show_as_index = i;
            show_as_combo.SelectedIndex(show_as_index);
        }
        show_as_combo.IsEnabled(!readonly);

        CheckBox private_check;
        private_check.Content(box_value(Tr(L"Privé (détails masqués aux personnes avec qui le calendrier est partagé)",
                                           L"Private (details hidden from people the calendar is shared with)")));
        private_check.IsChecked(existing && event.GetNamedBoolean(L"private", false));
        private_check.IsEnabled(!readonly);

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
        SetIconContent(teams_check, L"Microsoft_Office_Teams.svg",
            Tr(L"Réunion en ligne Microsoft Teams (lien de visio ajouté à l'invitation)",
               L"Microsoft Teams online meeting (join link added to the invitation)"));
        teams_check.IsChecked(teams);
        if (!existing && !readonly && !TeamsCalendarUid().empty())
            panel.Children().Append(teams_check);

        /* réunion Google Meet (nouvel événement d'un calendrier Gmail) ; exclusive avec Teams */
        CheckBox meet_check;
        SetIconContent(meet_check, L"Google_meet.svg",
            Tr(L"Réunion en ligne Google Meet (lien de visio ajouté à l'invitation)",
               L"Google Meet online meeting (join link added to the invitation)"));
        if (!existing && !readonly && !GoogleCalendarUid().empty())
            panel.Children().Append(meet_check);
        /* événement existant avec une réunion en ligne (Teams, Google Meet, Zoom) : bouton « Rejoindre » */
        if (existing)
        {
            hstring meeting_url = event.GetNamedString(L"online_meeting_url", L"");
            hstring meeting_kind = event.GetNamedString(L"online_meeting_kind", L"");
            if (!meeting_url.empty())
            {
                Button join_button;
                join_button.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
                if (meeting_kind == L"teams")
                    SetIconContent(join_button, L"Microsoft_Office_Teams.svg", Tr(L"Rejoindre la réunion Teams", L"Join the Teams meeting"));
                else if (meeting_kind == L"meet")
                    SetIconContent(join_button, L"Google_meet.svg", Tr(L"Rejoindre la réunion Google Meet", L"Join the Google Meet meeting"));
                else
                    SetIconContent(join_button, L"", Tr(L"Rejoindre la réunion en ligne", L"Join the online meeting"));
                join_button.Click([meeting_url](IInspectable const&, RoutedEventArgs const&)
                {
                    Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ meeting_url });
                });
                panel.Children().Append(join_button);
            }
        }
        teams_check.Checked([meet_check](IInspectable const&, RoutedEventArgs const&) { meet_check.IsChecked(false); });
        meet_check.Checked([teams_check](IInspectable const&, RoutedEventArgs const&) { teams_check.IsChecked(false); });

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


        /* ------------------------------------------------------------------ */
        /* Fenêtre de l'événement : barre de commandes (Enregistrer, Supprimer, tâche liée, Afficher */
        /* comme, Rappel, Périodicité, Classer, Privé, Importance) puis formulaire façon Outlook    */
        /* ------------------------------------------------------------------ */

        /* catégories de l'événement (modifiables par « Classer ») */
        auto categories = std::make_shared<std::vector<std::wstring>>(
            existing ? ::EvolutionWin::CategoryStore::CategoriesOf(event) : std::vector<std::wstring>{});
        StackPanel category_chips;
        auto refresh_chips = [this, category_chips, categories]()
        {
            category_chips.Children().Clear();
            category_chips.Children().Append(BuildCategoryChips(*categories));
        };
        refresh_chips();

        /* périodicité : règle RRULE, renvoyée seulement si l'utilisateur l'a modifiée */
        auto rrule = std::make_shared<std::wstring>(existing ? std::wstring{ event.GetNamedString(L"rrule", L"") } : std::wstring{});
        auto rrule_touched = std::make_shared<bool>(false);

        auto set_icon = [](auto const& button, hstring const& glyph, hstring const& label)
        {
            button.Label(label);
            FontIcon icon;
            icon.Glyph(glyph);
            button.Icon(icon);
        };

        AppBarButton save_button;
        set_icon(save_button, L"\xE74E", Tr(L"Enregistrer", L"Save"));
        save_button.IsEnabled(!readonly);
        {
            Microsoft::UI::Xaml::Input::KeyboardAccelerator accelerator;
            accelerator.Modifiers(Windows::System::VirtualKeyModifiers::Control);
            accelerator.Key(Windows::System::VirtualKey::S);
            save_button.KeyboardAccelerators().Append(accelerator);
        }
        AppBarButton delete_button;
        set_icon(delete_button, L"\xE74D", Tr(L"Supprimer", L"Delete"));
        delete_button.IsEnabled(existing && !readonly);
        AppBarButton task_button;
        set_icon(task_button, L"\xE9D5", Tr(L"Créer une tâche", L"Create a task"));
        ToolTipService::SetToolTip(task_button, box_value(Tr(L"Créer une tâche liée à cet événement", L"Create a task linked to this event")));
        task_button.IsEnabled(existing);
        task_button.Click([this, event, window](IInspectable const&, RoutedEventArgs const&)
        {
            window.Close();
            CreateTaskFromEvent(event);
        });
        AppBarButton close_button;
        set_icon(close_button, L"\xE711", Tr(L"Fermer", L"Close"));

        /* Afficher comme : états d'Outlook (la liste déroulante cachée garde la valeur enregistrée) */
        AppBarButton show_as_button;
        set_icon(show_as_button, L"\xE787", Tr(L"Afficher comme", L"Show as"));
        show_as_button.IsEnabled(!readonly);
        auto update_show_as = [show_as_button, show_as_combo]()
        {
            show_as_button.Label(Tr(L"Afficher comme : ", L"Show as: ") + unbox_value<hstring>(show_as_combo.SelectedItem()));
        };
        update_show_as();
        {
            MenuFlyout menu;
            for (uint32_t i = 0; i < show_as_combo.Items().Size(); ++i)
            {
                RadioMenuFlyoutItem item;
                item.Text(unbox_value<hstring>(show_as_combo.Items().GetAt(i)));
                item.GroupName(L"event_show_as");
                item.IsChecked(static_cast<int>(i) == show_as_combo.SelectedIndex());
                item.Click([show_as_combo, update_show_as, i](IInspectable const&, RoutedEventArgs const&)
                {
                    show_as_combo.SelectedIndex(static_cast<int>(i));
                    update_show_as();
                });
                menu.Items().Append(item);
            }
            show_as_button.Flyout(menu);
        }

        /* Rappel */
        AppBarButton reminder_button;
        set_icon(reminder_button, L"\xEA8F", Tr(L"Rappel", L"Reminder"));
        reminder_button.IsEnabled(!readonly);
        auto update_reminder = [reminder_button, reminder_combo]()
        {
            reminder_button.Label(Tr(L"Rappel : ", L"Reminder: ") + unbox_value<hstring>(reminder_combo.SelectedItem()));
        };
        update_reminder();
        {
            MenuFlyout menu;
            for (uint32_t i = 0; i < reminder_combo.Items().Size(); ++i)
            {
                RadioMenuFlyoutItem item;
                item.Text(unbox_value<hstring>(reminder_combo.Items().GetAt(i)));
                item.GroupName(L"event_reminder");
                item.IsChecked(static_cast<int>(i) == reminder_combo.SelectedIndex());
                item.Click([reminder_combo, update_reminder, i](IInspectable const&, RoutedEventArgs const&)
                {
                    reminder_combo.SelectedIndex(static_cast<int>(i));
                    update_reminder();
                });
                menu.Items().Append(item);
            }
            reminder_button.Flyout(menu);
        }

        /* Périodicité */
        AppBarButton recurrence_button;
        set_icon(recurrence_button, L"\xE72C", Tr(L"Périodicité", L"Recurrence"));
        recurrence_button.IsEnabled(!readonly);
        Flyout recurrence_flyout;
        {
            ComboBox freq_combo;
            freq_combo.Header(box_value(Tr(L"Répétition", L"Repeat")));
            freq_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
            for (auto const& label : { Tr(L"Aucune", L"None"), Tr(L"Tous les jours", L"Daily"), Tr(L"Toutes les semaines", L"Weekly"),
                                       Tr(L"Tous les mois", L"Monthly"), Tr(L"Tous les ans", L"Yearly") })
                freq_combo.Items().Append(box_value(label));
            NumberBox interval_box;
            interval_box.Header(box_value(Tr(L"Tous les (intervalle)", L"Every (interval)")));
            interval_box.Minimum(1);
            interval_box.Maximum(99);
            interval_box.Value(1);
            interval_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
            ComboBox end_combo;
            end_combo.Header(box_value(Tr(L"Fin", L"End")));
            end_combo.HorizontalAlignment(HorizontalAlignment::Stretch);
            end_combo.Items().Append(box_value(Tr(L"Jamais", L"Never")));
            end_combo.Items().Append(box_value(Tr(L"Après un nombre d'occurrences", L"After a number of occurrences")));
            NumberBox count_box;
            count_box.Header(box_value(Tr(L"Nombre d'occurrences", L"Number of occurrences")));
            count_box.Minimum(1);
            count_box.Maximum(999);
            count_box.Value(10);
            count_box.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Inline);
            TextBlock note;
            note.TextWrapping(TextWrapping::Wrap);
            note.MaxWidth(280);
            Windows::UI::Color grey{ 255, 128, 128, 128 };
            note.Foreground(SolidColorBrush(grey));

            /* état initial d'après la règle existante */
            {
                std::wstring rule = *rrule;
                auto value_of = [&rule](std::wstring const& key) -> std::wstring
                {
                    size_t at = rule.find(key + L"=");
                    if (at == std::wstring::npos)
                        return L"";
                    size_t from = at + key.size() + 1;
                    size_t to = rule.find(L';', from);
                    return rule.substr(from, to == std::wstring::npos ? std::wstring::npos : to - from);
                };
                std::wstring freq = value_of(L"FREQ");
                freq_combo.SelectedIndex(freq == L"DAILY" ? 1 : freq == L"WEEKLY" ? 2 : freq == L"MONTHLY" ? 3 : freq == L"YEARLY" ? 4 : 0);
                if (!value_of(L"INTERVAL").empty())
                    interval_box.Value(_wtof(value_of(L"INTERVAL").c_str()));
                if (!value_of(L"COUNT").empty())
                {
                    end_combo.SelectedIndex(1);
                    count_box.Value(_wtof(value_of(L"COUNT").c_str()));
                }
                else
                    end_combo.SelectedIndex(0);
                if (!rule.empty() && (rule.find(L"BYDAY") != std::wstring::npos || rule.find(L"UNTIL") != std::wstring::npos ||
                                      rule.find(L"BYMONTH") != std::wstring::npos || rule.find(L"BYSETPOS") != std::wstring::npos))
                    note.Text(Tr(L"Règle personnalisée : elle est conservée tant que vous ne modifiez pas ces réglages.",
                                 L"Custom rule: it is kept as long as you do not change these settings."));
            }
            count_box.IsEnabled(end_combo.SelectedIndex() == 1);

            /* recompose la règle à chaque changement */
            auto rebuild = [rrule, rrule_touched, freq_combo, interval_box, end_combo, count_box, note]()
            {
                *rrule_touched = true;
                note.Text(L"");
                count_box.IsEnabled(end_combo.SelectedIndex() == 1);
                static const wchar_t* const freqs[] = { L"", L"DAILY", L"WEEKLY", L"MONTHLY", L"YEARLY" };
                int index = freq_combo.SelectedIndex();
                if (index <= 0 || index > 4)
                {
                    rrule->clear();
                    return;
                }
                std::wstring rule = std::wstring{ L"FREQ=" } + freqs[index];
                int interval = static_cast<int>(interval_box.Value() >= 1 ? interval_box.Value() : 1);
                if (interval > 1)
                    rule += L";INTERVAL=" + std::to_wstring(interval);
                if (end_combo.SelectedIndex() == 1)
                    rule += L";COUNT=" + std::to_wstring(static_cast<int>(count_box.Value() >= 1 ? count_box.Value() : 1));
                *rrule = rule;
            };
            freq_combo.SelectionChanged([rebuild](auto&&, auto&&) { rebuild(); });
            end_combo.SelectionChanged([rebuild](auto&&, auto&&) { rebuild(); });
            interval_box.ValueChanged([rebuild](auto&&, auto&&) { rebuild(); });
            count_box.ValueChanged([rebuild](auto&&, auto&&) { rebuild(); });

            StackPanel flyout_panel;
            flyout_panel.Spacing(8);
            flyout_panel.Width(300);
            flyout_panel.Children().Append(freq_combo);
            flyout_panel.Children().Append(interval_box);
            flyout_panel.Children().Append(end_combo);
            flyout_panel.Children().Append(count_box);
            flyout_panel.Children().Append(note);
            recurrence_flyout.Content(flyout_panel);
            recurrence_button.Flyout(recurrence_flyout);
        }

        /* Classer : catégories */
        AppBarButton classify_button;
        set_icon(classify_button, L"\xE8EC", Tr(L"Classer", L"Categorise"));
        classify_button.IsEnabled(!readonly);
        MenuFlyout classify_flyout;
        classify_button.Flyout(classify_flyout);
        auto repopulate = std::make_shared<std::function<void()>>();
        *repopulate = [this, classify_flyout, categories, refresh_chips, repopulate]()
        {
            PopulateItemCategoryMenu(classify_flyout.Items(), *categories,
                [categories, refresh_chips, repopulate](std::vector<std::wstring> const& names)
                {
                    *categories = names;
                    refresh_chips();
                    (*repopulate)();
                });
        };
        (*repopulate)();

        AppBarToggleButton private_toggle;
        set_icon(private_toggle, L"\xE72E", Tr(L"Privé", L"Private"));
        private_toggle.IsChecked(existing && event.GetNamedBoolean(L"private", false));
        private_toggle.IsEnabled(!readonly);
        /* « private_check » reste la source de la valeur enregistrée */
        private_check.IsChecked(private_toggle.IsChecked());
        private_toggle.Click([private_toggle, private_check](auto&&, auto&&) { private_check.IsChecked(private_toggle.IsChecked()); });

        Windows::UI::Color red{ 255, 232, 17, 35 };
        Windows::UI::Color blue{ 255, 0, 120, 212 };
        AppBarToggleButton importance_high;
        set_icon(importance_high, L"\xE783", Tr(L"Importance haute", L"High importance"));
        importance_high.Icon().as<FontIcon>().Foreground(SolidColorBrush(red));
        AppBarToggleButton importance_low;
        set_icon(importance_low, L"\xE74B", Tr(L"Importance faible", L"Low importance"));
        importance_low.Icon().as<FontIcon>().Foreground(SolidColorBrush(blue));
        {
            hstring importance = existing ? event.GetNamedString(L"importance", L"normal") : hstring{ L"normal" };
            importance_high.IsChecked(importance == L"high");
            importance_low.IsChecked(importance == L"low");
        }
        importance_high.IsEnabled(!readonly);
        importance_low.IsEnabled(!readonly);
        importance_high.Click([importance_high, importance_low](auto&&, auto&&) { if (importance_high.IsChecked().Value()) importance_low.IsChecked(false); });
        importance_low.Click([importance_high, importance_low](auto&&, auto&&) { if (importance_low.IsChecked().Value()) importance_high.IsChecked(false); });

        CommandBar command_bar;
        command_bar.DefaultLabelPosition(CommandBarDefaultLabelPosition::Right);
        command_bar.HorizontalAlignment(HorizontalAlignment::Stretch);
        command_bar.IsOpen(false);
        {
            auto commands = command_bar.PrimaryCommands();
            commands.Append(save_button);
            if (existing)
            {
                commands.Append(delete_button);
                commands.Append(task_button);
            }
            commands.Append(close_button);
            commands.Append(AppBarSeparator{});
            commands.Append(show_as_button);
            commands.Append(reminder_button);
            commands.Append(recurrence_button);
            commands.Append(AppBarSeparator{});
            commands.Append(classify_button);
            commands.Append(private_toggle);
            commands.Append(importance_high);
            commands.Append(importance_low);
        }

        /* ---- formulaire pleine largeur, sur le modèle d'un événement Outlook ---- */
        std::vector<UIElement> others;
        for (auto const& child : panel.Children())
            others.push_back(child);
        panel.Children().Clear();
        auto is_named = [&](UIElement const& child)
        {
            for (UIElement const& named : std::initializer_list<UIElement>{
                     summary_box, location_box, calendar_combo, all_day_box, start_row, end_row, zone_combo, attendees_box,
                     teams_check, meet_check, sender_combo, invite_check, desc_box })
                if (named == child)
                    return true;
            return false;
        };

        summary_box.Header(nullptr);
        summary_box.PlaceholderText(Tr(L"Ajouter un titre", L"Add a title"));
        summary_box.FontSize(22);
        location_box.Header(nullptr);
        attendees_box.Header(nullptr);
        attendees_box.PlaceholderText(Tr(L"Ajouter des participants (adresses séparées par « ; »)", L"Add attendees (addresses separated by \";\")"));
        calendar_combo.Header(nullptr);
        sender_combo.Header(nullptr);
        zone_combo.Header(nullptr);
        start_date.Header(nullptr);
        end_date.Header(nullptr);
        start_time.Header(nullptr);
        end_time.Header(nullptr);
        desc_box.Header(nullptr);
        desc_box.Height(NAN);
        desc_box.MinHeight(160);
        desc_box.VerticalAlignment(VerticalAlignment::Stretch);
        desc_box.PlaceholderText(Tr(L"Description", L"Description"));

        /* date, heure et options sur une ligne : les sélecteurs sortent de leurs grilles de départ */
        start_row.Children().Clear();
        end_row.Children().Clear();
        StackPanel start_line;
        start_line.Orientation(Orientation::Horizontal);
        start_line.Spacing(10);
        start_line.Children().Append(start_date);
        start_line.Children().Append(start_time);
        start_line.Children().Append(zone_combo);
        start_line.Children().Append(all_day_box);
        StackPanel end_line;
        end_line.Orientation(Orientation::Horizontal);
        end_line.Spacing(10);
        end_line.Children().Append(end_date);
        end_line.Children().Append(end_time);
        HyperlinkButton make_recurring;
        make_recurring.Content(box_value(Tr(L"Rendre périodique", L"Make recurring")));
        make_recurring.IsEnabled(!readonly);
        make_recurring.Click([recurrence_flyout, make_recurring](auto&&, auto&&) { recurrence_flyout.ShowAt(make_recurring); });
        end_line.Children().Append(make_recurring);

        Grid form;
        form.Margin(ThicknessHelper::FromLengths(24, 12, 24, 16));
        form.RowSpacing(10);
        form.ColumnSpacing(12);
        {
            ColumnDefinition label_column, field_column;
            label_column.Width(GridLengthHelper::FromPixels(120));
            field_column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            form.ColumnDefinitions().Append(label_column);
            form.ColumnDefinitions().Append(field_column);
        }
        int form_row = 0;
        auto add_row = [&](hstring const& label, UIElement const& field, bool stretch_height)
        {
            RowDefinition row;
            row.Height(stretch_height ? GridLengthHelper::FromValueAndType(1, GridUnitType::Star) : GridLengthHelper::Auto());
            form.RowDefinitions().Append(row);
            if (!label.empty())
            {
                TextBlock text;
                text.Text(label);
                text.VerticalAlignment(stretch_height ? VerticalAlignment::Top : VerticalAlignment::Center);
                text.Margin(ThicknessHelper::FromLengths(0, stretch_height ? 8 : 0, 0, 0));
                text.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
                Grid::SetRow(text, form_row);
                form.Children().Append(text);
            }
            Grid::SetRow(field.as<FrameworkElement>(), form_row);
            Grid::SetColumn(field.as<FrameworkElement>(), 1);
            form.Children().Append(field);
            ++form_row;
        };

        /* informations (calendrier, organisateur, participants, catégories, bouton Rejoindre…) */
        StackPanel info_stack;
        info_stack.Spacing(6);
        info_stack.Children().Append(category_chips);
        for (auto const& child : others)
            if (!is_named(child))
                info_stack.Children().Append(child);
        {
            RowDefinition row;
            row.Height(GridLengthHelper::Auto());
            form.RowDefinitions().Append(row);
            Grid::SetRow(info_stack, form_row);
            Grid::SetColumnSpan(info_stack, 2);
            form.Children().Append(info_stack);
            ++form_row;
        }

        add_row(Tr(L"Titre", L"Title"), summary_box, false);
        if (std::find_if(others.begin(), others.end(), [&](UIElement const& c) { return c == calendar_combo; }) != others.end())
            add_row(Tr(L"Calendrier", L"Calendar"), calendar_combo, false);
        add_row(Tr(L"Participants", L"Attendees"), attendees_box, false);
        add_row(Tr(L"Heure de début", L"Start"), start_line, false);
        add_row(Tr(L"Heure de fin", L"End"), end_line, false);
        add_row(Tr(L"Emplacement", L"Location"), location_box, false);

        /* réunion en ligne et invitations */
        {
            StackPanel extras;
            extras.Spacing(8);
            bool any_extra = false;
            for (UIElement const& candidate : std::initializer_list<UIElement>{ teams_check, meet_check, sender_combo, invite_check })
                if (std::find_if(others.begin(), others.end(), [&](UIElement const& c) { return c == candidate; }) != others.end())
                {
                    if (candidate == sender_combo)
                    {
                        TextBlock sender_label;
                        sender_label.Text(Tr(L"Envoyer les invitations depuis", L"Send invitations from"));
                        sender_label.Foreground(Application::Current().Resources().Lookup(box_value(L"TextFillColorSecondaryBrush")).as<Brush>());
                        extras.Children().Append(sender_label);
                    }
                    extras.Children().Append(candidate);
                    any_extra = true;
                }
            if (any_extra)
                add_row(Tr(L"Réunion en ligne", L"Online meeting"), extras, false);
        }
        /* adresses web de la description : liens cliquables (ouverts par le navigateur du système) */
        StackPanel links_panel;
        links_panel.Spacing(2);
        {
            auto refresh_links = [links_panel, desc_box]()
            {
                links_panel.Children().Clear();
                std::wstring text{ desc_box.Text() };
                for (auto const& range : ::EvolutionWin::Helpers::FindUrlRanges(text))
                {
                    std::wstring url = text.substr(range.first, range.second);
                    HyperlinkButton link;
                    link.Content(box_value(hstring{ url }));
                    link.Padding(ThicknessHelper::FromLengths(0, 2, 0, 2));
                    link.Click([url](IInspectable const&, RoutedEventArgs const&)
                    {
                        Windows::System::Launcher::LaunchUriAsync(Windows::Foundation::Uri{ hstring{ url } });
                    });
                    links_panel.Children().Append(link);
                }
                links_panel.Visibility(links_panel.Children().Size() > 0 ? Visibility::Visible : Visibility::Collapsed);
            };
            refresh_links();
            desc_box.TextChanged([refresh_links](auto&&, auto&&) { refresh_links(); });
        }
        add_row(Tr(L"Description", L"Description"), desc_box, true);
        add_row(hstring{}, links_panel, false);

        Grid window_root;
        {
            RowDefinition title_row_def, bar_row_def, content_row_def;
            title_row_def.Height(GridLengthHelper::Auto());
            bar_row_def.Height(GridLengthHelper::Auto());
            content_row_def.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            window_root.RowDefinitions().Append(title_row_def);
            window_root.RowDefinitions().Append(bar_row_def);
            window_root.RowDefinitions().Append(content_row_def);
        }
        /* barre de titre personnalisée : icône et titre, comme les autres fenêtres de l'application */
        Grid title_bar;
        title_bar.Height(40);
        title_bar.Padding(ThicknessHelper::FromLengths(12, 0, 0, 0));
        {
            StackPanel title_content;
            title_content.Orientation(Orientation::Horizontal);
            title_content.VerticalAlignment(VerticalAlignment::Center);
            title_content.Spacing(8);
            FontIcon title_icon;
            title_icon.Glyph(L"\xE787");
            title_icon.FontSize(14);
            TextBlock title_text;
            title_text.Text(window_title);
            title_text.Style(Application::Current().Resources().Lookup(box_value(L"CaptionTextBlockStyle")).as<Style>());
            title_text.VerticalAlignment(VerticalAlignment::Center);
            title_content.Children().Append(title_icon);
            title_content.Children().Append(title_text);
            title_bar.Children().Append(title_content);
        }
        window.SetTitleBar(title_bar);
        Grid::SetRow(title_bar, 0);
        Grid::SetRow(command_bar, 1);
        ScrollViewer scroll;
        scroll.Content(form);
        scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        Grid::SetRow(scroll, 2);
        window_root.Children().Append(title_bar);
        window_root.Children().Append(command_bar);
        window_root.Children().Append(scroll);
        window.Content(window_root);

        /* enregistrement : retourne true si la fenêtre peut se fermer */
        auto commit = [=]() -> bool
        {
        if (readonly)
            return true;
        if (summary_box.Text().empty())
        {
            ShowStatus(Tr(L"Saisissez un titre pour l'événement.", L"Enter a title for the event."), InfoBarSeverity::Warning);
            summary_box.Focus(FocusState::Programmatic);
            return false;
        }

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
        if (!readonly)
            payload.SetNamedValue(L"show_as", JsonValue::CreateStringValue(
                show_as_values[show_as_combo.SelectedIndex() >= 0 && show_as_combo.SelectedIndex() < 5 ? show_as_combo.SelectedIndex() : 3]));
        if (!readonly)
            payload.SetNamedValue(L"private", JsonValue::CreateBooleanValue(private_check.IsChecked() && private_check.IsChecked().GetBoolean()));
        payload.SetNamedValue(L"description", JsonValue::CreateStringValue(desc_box.Text()));
        payload.SetNamedValue(L"categories", ::EvolutionWin::CategoryStore::ToJsonArray(*categories));
        payload.SetNamedValue(L"importance", JsonValue::CreateStringValue(
            importance_high.IsChecked().Value() ? L"high" : (importance_low.IsChecked().Value() ? L"low" : L"normal")));
        if (*rrule_touched)
            payload.SetNamedValue(L"rrule", JsonValue::CreateStringValue(hstring{ *rrule }));
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
                    ShowStatus(EventSaveErrorText(m_calendar_sources, edit_calendar, L"Modification de l'événement : ", error), InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(L"Événement modifié.", InfoBarSeverity::Success);
                LoadEvents();
                SendEventInvitations(sender_uid, edit_calendar, edit_uid);
            });
            return true;
        }

        hstring chosen_calendar = ActiveCalendarUid();
        if (calendar_combo.SelectedIndex() >= 0 && static_cast<size_t>(calendar_combo.SelectedIndex()) < calendar_uids.size())
            chosen_calendar = calendar_uids[calendar_combo.SelectedIndex()];
        if (!chosen_calendar.empty())
            payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(chosen_calendar));

        /* réunion Teams : l'événement doit être dans un calendrier Microsoft 365 (celui choisi s'il l'est, sinon un autre) */
        if (teams_check.IsChecked() && teams_check.IsChecked().GetBoolean())
        {
            bool chosen_is_m365 = false;
            for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
            {
                auto source = value.GetObject();
                if (source.GetNamedString(L"uid", L"") == chosen_calendar && source.GetNamedString(L"backend", L"") == L"microsoft365")
                    chosen_is_m365 = true;
            }
            if (!chosen_is_m365)
                if (hstring teams_calendar = TeamsCalendarUid(); !teams_calendar.empty())
                    payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(teams_calendar));
            payload.SetNamedValue(L"online_meeting", JsonValue::CreateBooleanValue(true));
        }

        /* réunion Google Meet : l'événement doit être dans un calendrier Gmail (celui choisi s'il l'est, sinon un autre) ;
         * le lien est ajouté par l'API Google Agenda une fois l'événement créé */
        bool want_meet = meet_check.IsChecked() && meet_check.IsChecked().GetBoolean();
        if (want_meet)
        {
            bool chosen_is_google = false;
            for (auto const& value : m_calendar_sources ? m_calendar_sources : JsonArray{})
            {
                auto source = value.GetObject();
                if (source.GetNamedString(L"uid", L"") == chosen_calendar && source.GetNamedString(L"backend", L"") == L"google")
                    chosen_is_google = true;
            }
            if (!chosen_is_google)
                if (hstring google_calendar = GoogleCalendarUid(); !google_calendar.empty())
                    payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(google_calendar));
        }

        hstring create_calendar = payload.GetNamedString(L"calendar_uid", L"");
        m_core->CreateEvent(payload.Stringify(), [this, sender_uid, create_calendar, want_meet](hstring const& json, hstring const& error)
        {
            if (!error.empty())
            {
                ShowStatus(EventSaveErrorText(m_calendar_sources, create_calendar, L"Création de l'événement : ", error), InfoBarSeverity::Error);
                return;
            }
            ShowStatus(L"Événement enregistré.", InfoBarSeverity::Success);
            LoadEvents();
            JsonObject created;
            if (JsonObject::TryParse(json, created))
            {
                hstring created_uid = created.GetNamedString(L"uid", L"");
                SendEventInvitations(sender_uid, create_calendar, created_uid);
                if (want_meet)
                {
                    ShowStatus(L"Ajout de la réunion Google Meet…", InfoBarSeverity::Informational);
                    m_core->AddGoogleMeet(create_calendar, created_uid, [this](hstring const& meet_json, hstring const& meet_error)
                    {
                        if (!meet_error.empty())
                        {
                            ShowStatus(L"Réunion Google Meet : " + meet_error, InfoBarSeverity::Error);
                            return;
                        }
                        JsonObject meet;
                        hstring link = JsonObject::TryParse(meet_json, meet) ? meet.GetNamedString(L"link", L"") : hstring{};
                        ShowStatus(link.empty() ? hstring{ L"Réunion Google Meet ajoutée (le lien apparaîtra après la synchronisation)." }
                                                : L"Réunion Google Meet ajoutée : " + link, InfoBarSeverity::Success);
                        LoadEvents();
                    });
                }
            }
        });
        return true;
        };

        save_button.Click([commit, window](auto&&, auto&&)
        {
            if (commit())
                window.Close();
        });
        delete_button.Click([this, event, window](auto&&, auto&&)
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
            window.Close();
        });
        close_button.Click([window](auto&&, auto&&) { window.Close(); });

        window.Activate();
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
