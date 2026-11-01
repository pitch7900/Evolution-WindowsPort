#include "pch.h"
#include "MainWindow.xaml.h"

#include <algorithm>
#include <vector>

/*
 * Grille du calendrier, à la manière d'Outlook : vues Jour, Semaine de
 * travail et Semaine (une colonne par jour, heures en lignes, événements en
 * blocs colorés côte à côte quand ils se chevauchent, journée entière en
 * haut) et vue Mois (six semaines, quelques événements par jour). Un clic
 * sur un événement l'ouvre (MainWindow::OpenCalendarEvent).
 */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace
{
    constexpr double HourHeight = 48;	/* hauteur d'une heure en vue Jour / Semaine */
    constexpr double GutterWidth = 56;	/* colonne des heures */
    constexpr int MonthChips = 3;	/* événements affichés par jour en vue Mois */

    Brush ThemeBrush(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(box_value(key)).as<Brush>();
    }

    Style ThemeStyle(wchar_t const* key)
    {
        return Application::Current().Resources().Lookup(box_value(key)).as<Style>();
    }

    /* couleur du calendrier ("#rrggbb"), sinon couleur d'accent */
    Windows::UI::Color CalendarColor(hstring const& value)
    {
        std::wstring color{ value };
        if (color.size() == 7 && color[0] == L'#')
        {
            auto channel = [&](size_t at) { return static_cast<uint8_t>(std::wcstoul(color.substr(at, 2).c_str(), nullptr, 16)); };
            return Microsoft::UI::ColorHelper::FromArgb(255, channel(1), channel(3), channel(5));
        }
        if (auto accent = ThemeBrush(L"AccentFillColorDefaultBrush").try_as<SolidColorBrush>())
            return accent.Color();
        return Microsoft::UI::Colors::SteelBlue();
    }

    SolidColorBrush CalendarBrush(hstring const& value, uint8_t alpha)
    {
        auto color = CalendarColor(value);
        color.A = alpha;
        return SolidColorBrush{ color };
    }

    /* début des jours [0, days] à partir de start_unix (minuit local, sûr
       pour les changements d'heure) */
    std::vector<int64_t> DayStarts(int64_t start_unix, int days)
    {
        std::vector<int64_t> starts;
        time_t raw = static_cast<time_t>(start_unix);
        struct tm local{};
        localtime_s(&local, &raw);

        for (int day = 0; day <= days; day++)
        {
            struct tm at = local;
            at.tm_mday += day;
            at.tm_hour = 0;
            at.tm_min = 0;
            at.tm_sec = 0;
            at.tm_isdst = -1;
            starts.push_back(static_cast<int64_t>(mktime(&at)));
        }
        return starts;
    }

    hstring FormatTime(int64_t unix_time)
    {
        static Windows::Globalization::DateTimeFormatting::DateTimeFormatter formatter{ L"shorttime" };
        return formatter.Format(winrt::clock::from_time_t(static_cast<time_t>(unix_time)));
    }

    hstring FormatWith(wchar_t const* pattern, int64_t unix_time)
    {
        return Windows::Globalization::DateTimeFormatting::DateTimeFormatter{ pattern }
            .Format(winrt::clock::from_time_t(static_cast<time_t>(unix_time)));
    }

    bool IsAllDay(JsonObject const& ev)
    {
        int64_t start = static_cast<int64_t>(ev.GetNamedNumber(L"start", 0));
        int64_t end = static_cast<int64_t>(ev.GetNamedNumber(L"end", 0));
        return ev.GetNamedBoolean(L"all_day", false) || end - start >= 24 * 3600;
    }

    /* une occurrence placée dans un jour de la grille */
    struct Segment
    {
        JsonObject event{ nullptr };
        int64_t start;	/* secondes depuis minuit */
        int64_t end;
        int lane{ 0 };
    };
}

namespace winrt::EvolutionWin::implementation
{
    /* Ouvre un événement (détail lu par evo-core, puis fenêtre). */
    void MainWindow::OpenCalendarEvent(hstring const& calendar_uid, hstring const& uid)
    {
        m_core->GetEvent(calendar_uid, uid, [this](hstring const& json, hstring const& error)
        {
            JsonObject details;
            if (!error.empty() || !JsonObject::TryParse(json, details))
            {
                ShowStatus(L"Ouverture de l'événement : " + error, InfoBarSeverity::Error);
                return;
            }
            ShowEventDialogAsync(details);
        });
    }

    /* Vues Jour / Semaine de travail / Semaine.
     * Paramètres :
     *   events     : occurrences de la période (evo_core_list_calendar_events)
     *   start_unix : minuit du premier jour
     *   days       : nombre de jours (1, 5 ou 7) */
    void MainWindow::RenderCalendarGrid(JsonArray const& events, int64_t start_unix, int days)
    {
        auto host = CalendarGridHost();
        host.Children().Clear();
        host.RowDefinitions().Clear();
        host.ColumnDefinitions().Clear();

        for (auto height : { GridLengthHelper::Auto(), GridLengthHelper::Auto(), GridLengthHelper::FromValueAndType(1, GridUnitType::Star) })
        {
            RowDefinition row;
            row.Height(height);
            host.RowDefinitions().Append(row);
        }

        auto day_starts = DayStarts(start_unix, days);
        int64_t now = static_cast<int64_t>(std::time(nullptr));

        auto add_columns = [days](Grid const& grid)
        {
            ColumnDefinition gutter;
            gutter.Width(GridLengthHelper::FromPixels(GutterWidth));
            grid.ColumnDefinitions().Append(gutter);
            for (int day = 0; day < days; day++)
                grid.ColumnDefinitions().Append(ColumnDefinition{});
        };

        auto open_on_tap = [this](UIElement const& element, JsonObject const& ev)
        {
            hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
            hstring uid = ev.GetNamedString(L"uid", L"");
            element.Tapped([this, calendar_uid, uid](IInspectable const&, Input::TappedRoutedEventArgs const& args)
            {
                args.Handled(true);
                OpenCalendarEvent(calendar_uid, uid);
            });
        };

        /* en-têtes des jours (« lun. 26 », aujourd'hui en couleur d'accent) */
        Grid headers;
        add_columns(headers);
        headers.Padding(ThicknessHelper::FromLengths(0, 0, 0, 4));
        for (int day = 0; day < days; day++)
        {
            bool today = now >= day_starts[day] && now < day_starts[day + 1];
            StackPanel header;
            header.Padding(ThicknessHelper::FromLengths(8, 0, 0, 0));
            TextBlock name;
            name.Text(FormatWith(L"dayofweek.abbreviated", day_starts[day]));
            name.Style(ThemeStyle(L"CaptionTextBlockStyle"));
            TextBlock number;
            number.Text(FormatWith(L"day", day_starts[day]));
            number.Style(ThemeStyle(L"SubtitleTextBlockStyle"));
            if (today)
            {
                name.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
                number.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
            }
            header.Children().Append(name);
            header.Children().Append(number);
            Grid::SetColumn(header, day + 1);
            headers.Children().Append(header);
        }
        host.Children().Append(headers);

        /* journée entière et événements de plusieurs jours, en haut */
        Grid all_day_band;
        add_columns(all_day_band);
        Grid::SetRow(all_day_band, 1);
        std::vector<StackPanel> all_day_columns;
        for (int day = 0; day < days; day++)
        {
            StackPanel column;
            column.Spacing(2);
            column.Padding(ThicknessHelper::FromLengths(2, 0, 2, 4));
            Grid::SetColumn(column, day + 1);
            all_day_band.Children().Append(column);
            all_day_columns.push_back(column);
        }
        host.Children().Append(all_day_band);

        /* occurrences avec heure, par jour */
        std::vector<std::vector<Segment>> segments(days);
        for (auto const& value : events)
        {
            auto ev = value.GetObject();
            int64_t start = static_cast<int64_t>(ev.GetNamedNumber(L"start", 0));
            int64_t end = static_cast<int64_t>(ev.GetNamedNumber(L"end", 0));
            if (end <= start)
                end = start + 1800;

            for (int day = 0; day < days; day++)
            {
                int64_t from = (std::max)(start, day_starts[day]);
                int64_t to = (std::min)(end, day_starts[day + 1]);
                if (to <= from && !(start == end && start >= day_starts[day] && start < day_starts[day + 1]))
                    continue;

                if (IsAllDay(ev))
                {
                    Border chip;
                    chip.Background(CalendarBrush(ev.GetNamedString(L"calendar_color", L""), 0x60));
                    chip.BorderBrush(CalendarBrush(ev.GetNamedString(L"calendar_color", L""), 0xFF));
                    chip.BorderThickness(ThicknessHelper::FromLengths(3, 0, 0, 0));
                    chip.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
                    chip.Padding(ThicknessHelper::FromLengths(4, 1, 4, 1));
                    TextBlock title;
                    title.Text(ev.GetNamedString(L"summary", L""));
                    title.FontSize(12);
                    title.TextTrimming(TextTrimming::CharacterEllipsis);
                    chip.Child(title);
                    ToolTipService::SetToolTip(chip, box_value(ev.GetNamedString(L"summary", L"") + L"\n" +
                        ev.GetNamedString(L"calendar_name", L"")));
                    open_on_tap(chip, ev);
                    all_day_columns[day].Children().Append(chip);
                    continue;
                }

                segments[day].push_back({ ev, from - day_starts[day], (std::max)(to, from + 900) - day_starts[day] });
            }
        }

        /* corps : 24 heures dans une zone qui défile */
        ScrollViewer scroll;
        Grid::SetRow(scroll, 2);
        Grid body;
        add_columns(body);
        body.Height(24 * HourHeight);

        for (int hour = 0; hour < 24; hour++)
        {
            Border line;
            line.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
            line.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
            line.Height(HourHeight);
            line.VerticalAlignment(VerticalAlignment::Top);
            line.Margin(ThicknessHelper::FromLengths(0, hour * HourHeight, 0, 0));
            Grid::SetColumn(line, 1);
            Grid::SetColumnSpan(line, days);
            body.Children().Append(line);

            TextBlock label;
            label.Text(hour < 10 ? L"0" + to_hstring(hour) + L":00" : to_hstring(hour) + L":00");
            label.Style(ThemeStyle(L"CaptionTextBlockStyle"));
            label.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
            label.VerticalAlignment(VerticalAlignment::Top);
            label.Margin(ThicknessHelper::FromLengths(8, hour * HourHeight + 2, 0, 0));
            body.Children().Append(label);
        }

        for (int day = 0; day < days; day++)
        {
            /* séparateur des jours */
            Border separator;
            separator.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
            separator.BorderThickness(ThicknessHelper::FromLengths(1, 0, 0, 0));
            Grid::SetColumn(separator, day + 1);
            body.Children().Append(separator);

            /* chevauchements : groupes d'événements qui se recouvrent, une
               colonne (« couloir ») par événement simultané dans le groupe */
            auto& list = segments[day];
            std::sort(list.begin(), list.end(), [](Segment const& a, Segment const& b)
            {
                return a.start != b.start ? a.start < b.start : a.end > b.end;
            });

            size_t first = 0;
            while (first < list.size())
            {
                size_t last = first;
                int64_t group_end = list[first].end;
                std::vector<int64_t> lane_ends;

                for (; last < list.size() && (last == first || list[last].start < group_end); last++)
                {
                    auto& seg = list[last];
                    int lane = 0;
                    while (lane < static_cast<int>(lane_ends.size()) && lane_ends[lane] > seg.start)
                        lane++;
                    if (lane == static_cast<int>(lane_ends.size()))
                        lane_ends.push_back(seg.end);
                    else
                        lane_ends[lane] = seg.end;
                    seg.lane = lane;
                    group_end = (std::max)(group_end, seg.end);
                }

                int64_t group_start = list[first].start;
                Grid group;
                group.VerticalAlignment(VerticalAlignment::Top);
                group.Margin(ThicknessHelper::FromLengths(2, group_start * HourHeight / 3600, 4, 0));
                group.Height((group_end - group_start) * HourHeight / 3600);
                for (size_t lane = 0; lane < lane_ends.size(); lane++)
                    group.ColumnDefinitions().Append(ColumnDefinition{});
                Grid::SetColumn(group, day + 1);

                for (size_t index = first; index < last; index++)
                {
                    auto const& seg = list[index];
                    auto ev = seg.event;
                    hstring color = ev.GetNamedString(L"calendar_color", L"");
                    int64_t event_start = static_cast<int64_t>(ev.GetNamedNumber(L"start", 0));
                    int64_t event_end = static_cast<int64_t>(ev.GetNamedNumber(L"end", 0));

                    Border block;
                    block.VerticalAlignment(VerticalAlignment::Top);
                    block.Margin(ThicknessHelper::FromLengths(0, (seg.start - group_start) * HourHeight / 3600, 2, 0));
                    block.Height((std::max)(18.0, (seg.end - seg.start) * HourHeight / 3600 - 2));
                    block.Background(CalendarBrush(color, 0x60));
                    block.BorderBrush(CalendarBrush(color, 0xFF));
                    block.BorderThickness(ThicknessHelper::FromLengths(3, 0, 0, 0));
                    block.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
                    block.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
                    Grid::SetColumn(block, seg.lane);

                    StackPanel content;
                    TextBlock title;
                    title.Text(ev.GetNamedString(L"summary", L""));
                    title.FontSize(12);
                    title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                    title.TextTrimming(TextTrimming::CharacterEllipsis);
                    content.Children().Append(title);

                    hstring when = FormatTime(event_start) + L" – " + FormatTime(event_end);
                    hstring location = ev.GetNamedString(L"location", L"");
                    TextBlock details;
                    details.Text(location.empty() ? when : when + L"  " + location);
                    details.FontSize(11);
                    details.TextTrimming(TextTrimming::CharacterEllipsis);
                    content.Children().Append(details);
                    block.Child(content);

                    ToolTipService::SetToolTip(block, box_value(ev.GetNamedString(L"summary", L"") + L"\n" + when +
                        (location.empty() ? hstring{} : L"\n" + location) + L"\n" + ev.GetNamedString(L"calendar_name", L"")));
                    open_on_tap(block, ev);
                    group.Children().Append(block);
                }

                body.Children().Append(group);
                first = last;
            }

            /* heure actuelle dans la colonne d'aujourd'hui */
            if (now >= day_starts[day] && now < day_starts[day + 1])
            {
                Border now_line;
                now_line.Height(2);
                now_line.VerticalAlignment(VerticalAlignment::Top);
                now_line.Margin(ThicknessHelper::FromLengths(0, (now - day_starts[day]) * HourHeight / 3600, 0, 0));
                now_line.Background(SolidColorBrush{ Microsoft::UI::Colors::Red() });
                Grid::SetColumn(now_line, day + 1);
                body.Children().Append(now_line);
            }
        }

        scroll.Content(body);
        /* début de journée visible (7 h) */
        scroll.Loaded([scroll](IInspectable const&, RoutedEventArgs const&)
        {
            scroll.ChangeView(nullptr, IReference<double>{ 7 * HourHeight }, nullptr, true);
        });
        host.Children().Append(scroll);
    }

    /* Vue Mois : six semaines à partir du lundi qui précède le 1er.
     * Paramètres :
     *   events     : occurrences de la période
     *   start_unix : minuit du premier jour affiché
     *   month      : mois affiché (0-11), les autres jours sont estompés */
    void MainWindow::RenderMonthGrid(JsonArray const& events, int64_t start_unix, int month)
    {
        constexpr int Weeks = 6;
        auto host = CalendarGridHost();
        host.Children().Clear();
        host.RowDefinitions().Clear();
        host.ColumnDefinitions().Clear();

        RowDefinition names_row;
        names_row.Height(GridLengthHelper::Auto());
        host.RowDefinitions().Append(names_row);
        for (int week = 0; week < Weeks; week++)
            host.RowDefinitions().Append(RowDefinition{});
        for (int day = 0; day < 7; day++)
            host.ColumnDefinitions().Append(ColumnDefinition{});

        auto day_starts = DayStarts(start_unix, Weeks * 7);
        int64_t now = static_cast<int64_t>(std::time(nullptr));

        for (int day = 0; day < 7; day++)
        {
            TextBlock name;
            name.Text(FormatWith(L"dayofweek.full", day_starts[day]));
            name.Style(ThemeStyle(L"CaptionTextBlockStyle"));
            name.Margin(ThicknessHelper::FromLengths(6, 0, 0, 4));
            Grid::SetColumn(name, day);
            host.Children().Append(name);
        }

        for (int index = 0; index < Weeks * 7; index++)
        {
            int64_t from = day_starts[index];
            int64_t to = day_starts[index + 1];
            time_t raw = static_cast<time_t>(from);
            struct tm local{};
            localtime_s(&local, &raw);
            bool today = now >= from && now < to;

            Border cell;
            cell.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
            cell.BorderThickness(ThicknessHelper::FromLengths(index % 7 == 0 ? 0 : 1, 1, 0, 0));
            cell.Padding(ThicknessHelper::FromLengths(4, 2, 4, 2));
            if (local.tm_mon != month)
                cell.Background(ThemeBrush(L"SubtleFillColorSecondaryBrush"));
            Grid::SetRow(cell, index / 7 + 1);
            Grid::SetColumn(cell, index % 7);

            StackPanel content;
            content.Spacing(2);

            /* numéro du jour : clic = vue Jour */
            TextBlock number;
            number.Text(to_hstring(local.tm_mday));
            number.FontWeight(today ? Microsoft::UI::Text::FontWeights::Bold() : Microsoft::UI::Text::FontWeights::Normal());
            if (today)
                number.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
            else if (local.tm_mon != month)
                number.Foreground(ThemeBrush(L"TextFillColorTertiaryBrush"));
            ToolTipService::SetToolTip(number, box_value(L"Afficher ce jour"));
            number.Tapped([this, from](IInspectable const&, Input::TappedRoutedEventArgs const& args)
            {
                args.Handled(true);
                m_selected_day_start = from;
                SetCalendarPeriod(L"day");
            });
            content.Children().Append(number);

            int shown = 0, hidden = 0;
            for (auto const& value : events)
            {
                auto ev = value.GetObject();
                int64_t start = static_cast<int64_t>(ev.GetNamedNumber(L"start", 0));
                int64_t end = static_cast<int64_t>(ev.GetNamedNumber(L"end", 0));
                bool in_day = (start < to && end > from) || (start == end && start >= from && start < to);
                if (!in_day)
                    continue;

                if (shown == MonthChips)
                {
                    hidden++;
                    continue;
                }
                shown++;

                hstring color = ev.GetNamedString(L"calendar_color", L"");
                bool all_day = IsAllDay(ev);
                Border chip;
                chip.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
                chip.Padding(ThicknessHelper::FromLengths(4, 0, 4, 0));
                if (all_day)
                    chip.Background(CalendarBrush(color, 0x60));

                StackPanel line;
                line.Orientation(Orientation::Horizontal);
                line.Spacing(4);
                if (!all_day)
                {
                    Border dot;
                    dot.Width(8);
                    dot.Height(8);
                    dot.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                    dot.Background(CalendarBrush(color, 0xFF));
                    dot.VerticalAlignment(VerticalAlignment::Center);
                    line.Children().Append(dot);
                }
                TextBlock title;
                title.Text(all_day ? ev.GetNamedString(L"summary", L"")
                                   : FormatTime(start) + L" " + ev.GetNamedString(L"summary", L""));
                title.FontSize(12);
                title.TextTrimming(TextTrimming::CharacterEllipsis);
                line.Children().Append(title);
                chip.Child(line);

                ToolTipService::SetToolTip(chip, box_value(ev.GetNamedString(L"summary", L"") + L"\n" +
                    ev.GetNamedString(L"calendar_name", L"")));
                hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
                hstring uid = ev.GetNamedString(L"uid", L"");
                chip.Tapped([this, calendar_uid, uid](IInspectable const&, Input::TappedRoutedEventArgs const& args)
                {
                    args.Handled(true);
                    OpenCalendarEvent(calendar_uid, uid);
                });
                content.Children().Append(chip);
            }

            if (hidden > 0)
            {
                TextBlock more;
                more.Text(L"+" + to_hstring(hidden) + (hidden > 1 ? L" autres" : L" autre"));
                more.Style(ThemeStyle(L"CaptionTextBlockStyle"));
                more.Foreground(ThemeBrush(L"AccentTextFillColorPrimaryBrush"));
                more.Tapped([this, from](IInspectable const&, Input::TappedRoutedEventArgs const& args)
                {
                    args.Handled(true);
                    m_selected_day_start = from;
                    SetCalendarPeriod(L"day");
                });
                content.Children().Append(more);
            }

            cell.Child(content);
            host.Children().Append(cell);
        }
    }
}
