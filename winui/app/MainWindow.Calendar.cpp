#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Settings.h"
#include "CategoryStore.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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
using namespace ::EvolutionWin::Helpers;

namespace
{
    /**
     * @brief Retourne le pas horaire (en minutes) configuré pour la grille du calendrier.
     * @return 15, 30 ou 60 (défaut : 30 minutes).
     */
    int GetCalendarTimeScale()
    {
        std::wstring scale_str{ ::EvolutionWin::Settings::Instance().GetString(L"calendar.time-scale", L"30") };
        int scale = 30;
        try { scale = std::stoi(scale_str); } catch (...) { scale = 30; }
        if (scale != 15 && scale != 30 && scale != 60)
            scale = 30;
        return scale;
    }

    /**
     * @brief Calcule la hauteur d'une heure en pixels selon le pas horaire choisi.
     * @param scale_minutes Pas horaire (15, 30 ou 60 minutes).
     * @return Hauteur d'une heure en pixels.
     */
    double GetCalendarHourHeight(int scale_minutes)
    {
        switch (scale_minutes)
        {
        case 15:
            return 120.0;
        case 60:
            return 48.0;
        case 30:
        default:
            return 72.0;
        }
    }
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
                ShowStatus(Fmt(Tr(L"Ouverture de l'événement : {0}"), { error }), InfoBarSeverity::Error);
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

        /* Configuration du second fuseau horaire dans la grille */
        bool second_tz_enabled = ::EvolutionWin::Settings::Instance().GetBoolean(L"calendar.second-timezone-enabled", false);
        std::wstring second_tz{ ::EvolutionWin::Settings::Instance().GetString(L"calendar.second-timezone", L"UTC") };
        double gutter_width = second_tz_enabled ? 96.0 : GutterWidth;
        int local_utc_offset = GetLocalUtcOffsetHours();
        int target_utc_offset = GetTimezoneUtcOffsetHours(second_tz);
        std::wstring target_tz_label = GetTimezoneShortLabel(second_tz);

        auto add_columns = [days, gutter_width](Grid const& grid)
        {
            ColumnDefinition gutter;
            gutter.Width(GridLengthHelper::FromPixels(gutter_width));
            grid.ColumnDefinitions().Append(gutter);
            for (int day = 0; day < days; day++)
                grid.ColumnDefinitions().Append(ColumnDefinition{});
        };

        auto open_on_tap = [this](UIElement const& element, JsonObject const& ev)
        {
            hstring calendar_uid = ev.GetNamedString(L"calendar_uid", L"");
            hstring uid = ev.GetNamedString(L"uid", L"");
            /* simple clic : sans effet (le clic ne crée pas de sélection) ; double clic : ouvre la fenêtre de l'événement */
            element.Tapped([](IInspectable const&, Input::TappedRoutedEventArgs const& args) { args.Handled(true); });
            element.DoubleTapped([this, calendar_uid, uid](IInspectable const&, Input::DoubleTappedRoutedEventArgs const& args)
            {
                args.Handled(true);
                OpenCalendarEvent(calendar_uid, uid);
            });
            AttachEventContextMenu(element, ev);
            EnableEventExportDrag(element, ev);
        };

        /* en-têtes des jours (« lun. 26 », aujourd'hui en couleur d'accent) */
        Grid headers;
        add_columns(headers);
        headers.Padding(ThicknessHelper::FromLengths(0, 0, 0, 4));

        if (second_tz_enabled)
        {
            Grid tz_headers;
            ColumnDefinition tz_c1, tz_c2;
            tz_c1.Width(GridLengthHelper::FromPixels(46));
            tz_c2.Width(GridLengthHelper::FromPixels(50));
            tz_headers.ColumnDefinitions().Append(tz_c1);
            tz_headers.ColumnDefinitions().Append(tz_c2);
            tz_headers.Padding(ThicknessHelper::FromLengths(6, 4, 0, 0));

            TextBlock tz2_title;
            tz2_title.Text(hstring{ target_tz_label });
            tz2_title.Style(ThemeStyle(L"CaptionTextBlockStyle"));
            tz2_title.Foreground(ThemeBrush(L"TextFillColorTertiaryBrush"));
            tz2_title.FontSize(10);
            tz_headers.Children().Append(tz2_title);

            TextBlock tz1_title;
            tz1_title.Text(Tr(L"Local"));
            tz1_title.Style(ThemeStyle(L"CaptionTextBlockStyle"));
            tz1_title.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
            tz1_title.FontSize(10);
            Grid::SetColumn(tz1_title, 1);
            tz_headers.Children().Append(tz1_title);

            headers.Children().Append(tz_headers);
        }
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
                    chip.Background(CalendarBrush(::EvolutionWin::CategoryStore::EventColorHex(ev), 0x60));
                    chip.BorderBrush(CalendarBrush(::EvolutionWin::CategoryStore::EventColorHex(ev), 0xFF));
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

        const int time_scale = GetCalendarTimeScale();
        const double HourHeight = GetCalendarHourHeight(time_scale);

        /* corps : 24 heures dans une zone qui défile */
        ScrollViewer scroll;
        Grid::SetRow(scroll, 2);
        Grid body;
        add_columns(body);
        body.Height(24 * HourHeight);

        if (second_tz_enabled)
        {
            Border tz_sep;
            tz_sep.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
            tz_sep.BorderThickness(ThicknessHelper::FromLengths(1, 0, 0, 0));
            tz_sep.Height(24 * HourHeight);
            tz_sep.HorizontalAlignment(HorizontalAlignment::Left);
            tz_sep.Margin(ThicknessHelper::FromLengths(46, 0, 0, 0));
            body.Children().Append(tz_sep);
        }

        for (int hour = 0; hour < 24; hour++)
        {
            // Ligne principale d'heure
            Border line;
            line.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
            line.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
            line.Height(HourHeight);
            line.VerticalAlignment(VerticalAlignment::Top);
            line.Margin(ThicknessHelper::FromLengths(0, hour * HourHeight, 0, 0));
            Grid::SetColumn(line, 1);
            Grid::SetColumnSpan(line, days);
            body.Children().Append(line);

            if (second_tz_enabled)
            {
                int h2 = CalculateEquivalentHour(hour, local_utc_offset, target_utc_offset);
                TextBlock label2;
                label2.Text(h2 < 10 ? L"0" + to_hstring(h2) + L":00" : to_hstring(h2) + L":00");
                label2.Style(ThemeStyle(L"CaptionTextBlockStyle"));
                label2.Foreground(ThemeBrush(L"TextFillColorTertiaryBrush"));
                label2.VerticalAlignment(VerticalAlignment::Top);
                label2.Margin(ThicknessHelper::FromLengths(6, hour * HourHeight + 2, 0, 0));
                body.Children().Append(label2);
            }

            TextBlock label;
            label.Text(hour < 10 ? L"0" + to_hstring(hour) + L":00" : to_hstring(hour) + L":00");
            label.Style(ThemeStyle(L"CaptionTextBlockStyle"));
            label.Foreground(ThemeBrush(L"TextFillColorSecondaryBrush"));
            label.VerticalAlignment(VerticalAlignment::Top);
            label.Margin(ThicknessHelper::FromLengths(second_tz_enabled ? 52 : 8, hour * HourHeight + 2, 0, 0));
            body.Children().Append(label);

            // Graduations intermédiaires selon le pas horaire (15 ou 30 min)
            if (time_scale == 30)
            {
                Border sub_line;
                sub_line.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
                sub_line.Opacity(0.4);
                sub_line.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
                sub_line.Height(HourHeight / 2);
                sub_line.VerticalAlignment(VerticalAlignment::Top);
                sub_line.Margin(ThicknessHelper::FromLengths(0, hour * HourHeight + HourHeight / 2, 0, 0));
                Grid::SetColumn(sub_line, 1);
                Grid::SetColumnSpan(sub_line, days);
                body.Children().Append(sub_line);

                TextBlock sub_label;
                sub_label.Text(L":30");
                sub_label.Style(ThemeStyle(L"CaptionTextBlockStyle"));
                sub_label.Foreground(ThemeBrush(L"TextFillColorTertiaryBrush"));
                sub_label.FontSize(10);
                sub_label.VerticalAlignment(VerticalAlignment::Top);
                sub_label.Margin(ThicknessHelper::FromLengths(second_tz_enabled ? 72 : 28, hour * HourHeight + HourHeight / 2 + 1, 0, 0));
                body.Children().Append(sub_label);
            }
            else if (time_scale == 15)
            {
                for (int slot = 1; slot <= 3; slot++)
                {
                    Border sub_line;
                    sub_line.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
                    sub_line.Opacity(slot == 2 ? 0.45 : 0.25);
                    sub_line.BorderThickness(ThicknessHelper::FromLengths(0, 1, 0, 0));
                    sub_line.Height(HourHeight / 4);
                    sub_line.VerticalAlignment(VerticalAlignment::Top);
                    sub_line.Margin(ThicknessHelper::FromLengths(0, hour * HourHeight + slot * (HourHeight / 4), 0, 0));
                    Grid::SetColumn(sub_line, 1);
                    Grid::SetColumnSpan(sub_line, days);
                    body.Children().Append(sub_line);

                    TextBlock sub_label;
                    sub_label.Text(L":" + to_hstring(slot * 15));
                    sub_label.Style(ThemeStyle(L"CaptionTextBlockStyle"));
                    sub_label.Foreground(ThemeBrush(L"TextFillColorTertiaryBrush"));
                    sub_label.FontSize(9);
                    sub_label.VerticalAlignment(VerticalAlignment::Top);
                    sub_label.Margin(ThicknessHelper::FromLengths(second_tz_enabled ? 70 : 26, hour * HourHeight + slot * (HourHeight / 4) + 1, 0, 0));
                    body.Children().Append(sub_label);
                }
            }
        }

        for (int day = 0; day < days; day++)
        {
            /* séparateur des jours */
            Border separator;
            separator.BorderBrush(ThemeBrush(L"DividerStrokeColorDefaultBrush"));
            separator.BorderThickness(ThicknessHelper::FromLengths(1, 0, 0, 0));
            Grid::SetColumn(separator, day + 1);
            body.Children().Append(separator);

            /* sélection d'un créneau à la souris (glisser) ou clic simple (1 h) : crée un événement.
               Couche ajoutée avant les événements, qui restent cliquables au-dessus. */
            {
                Grid slot_layer;
                slot_layer.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
                Grid::SetColumn(slot_layer, day + 1);

                Border selection;
                selection.VerticalAlignment(VerticalAlignment::Top);
                selection.Visibility(Visibility::Collapsed);
                selection.IsHitTestVisible(false);
                selection.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
                selection.Background(ThemeBrush(L"AccentFillColorSecondaryBrush"));
                selection.Opacity(0.45);
                selection.Margin(ThicknessHelper::FromLengths(2, 0, 4, 0));
                slot_layer.Children().Append(selection);

                struct Drag { bool active{ false }; double y0{ 0 }; };
                auto drag = std::make_shared<Drag>();
                int64_t day_start = day_starts[day];
                double hour_height = HourHeight;

                auto snap = [hour_height](double y, bool up) -> int64_t
                {
                    double minutes = (std::max)(0.0, y) / hour_height * 60.0;
                    int64_t quarters = static_cast<int64_t>(up ? std::ceil(minutes / 15.0) : std::floor(minutes / 15.0));
                    return (std::min<int64_t>)(quarters, 96) * 900;
                };

                slot_layer.PointerPressed([drag, slot_layer, selection, snap, hour_height](IInspectable const&, Input::PointerRoutedEventArgs const& args)
                {
                    auto point = args.GetCurrentPoint(slot_layer);
                    if (!point.Properties().IsLeftButtonPressed())
                        return;
                    drag->active = true;
                    drag->y0 = point.Position().Y;
                    slot_layer.CapturePointer(args.Pointer());
                    int64_t from_s = snap(drag->y0, false);
                    selection.Margin(ThicknessHelper::FromLengths(2, from_s * hour_height / 3600, 4, 0));
                    selection.Height(hour_height / 4);
                    selection.Visibility(Visibility::Visible);
                });
                slot_layer.PointerMoved([drag, slot_layer, selection, snap, hour_height](IInspectable const&, Input::PointerRoutedEventArgs const& args)
                {
                    if (!drag->active)
                        return;
                    double y1 = args.GetCurrentPoint(slot_layer).Position().Y;
                    int64_t a = snap((std::min)(drag->y0, y1), false);
                    int64_t b = snap((std::max)(drag->y0, y1), true);
                    if (b <= a)
                        b = a + 900;
                    selection.Margin(ThicknessHelper::FromLengths(2, a * hour_height / 3600, 4, 0));
                    selection.Height((b - a) * hour_height / 3600);
                });
                slot_layer.PointerReleased([this, drag, slot_layer, selection, snap, day_start](IInspectable const&, Input::PointerRoutedEventArgs const& args)
                {
                    if (!drag->active)
                        return;
                    drag->active = false;
                    slot_layer.ReleasePointerCapture(args.Pointer());
                    selection.Visibility(Visibility::Collapsed);

                    double y1 = args.GetCurrentPoint(slot_layer).Position().Y;
                    int64_t a, b;
                    if (std::abs(y1 - drag->y0) < 6.0)
                    {
                        /* simple clic : créneau d'une heure à partir de la demi-heure la plus proche */
                        int64_t half = (snap(drag->y0, false) / 1800) * 1800;
                        a = half;
                        b = a + 3600;
                    }
                    else
                    {
                        a = snap((std::min)(drag->y0, y1), false);
                        b = snap((std::max)(drag->y0, y1), true);
                        if (b <= a)
                            b = a + 900;
                    }
                    BeginEventFromSelection(day_start + a, day_start + b);
                });
                slot_layer.PointerCanceled([drag, selection](IInspectable const&, Input::PointerRoutedEventArgs const&)
                {
                    drag->active = false;
                    selection.Visibility(Visibility::Collapsed);
                });
                ToolTipService::SetToolTip(slot_layer, box_value(Tr(L"Cliquer ou glisser pour créer un événement")));
                body.Children().Append(slot_layer);
            }

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
                    hstring color = ::EvolutionWin::CategoryStore::EventColorHex(ev);
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
        /* début de journée visible calé sur les heures de travail configurées */
        double work_start = ::EvolutionWin::Settings::Instance().GetNumber(L"calendar.work-start", 8.0);
        scroll.Loaded([scroll, work_start, HourHeight](IInspectable const&, RoutedEventArgs const&)
        {
            scroll.ChangeView(nullptr, IReference<double>{ (std::max)(0.0, work_start - 1.0) * HourHeight }, nullptr, true);
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
            if (cell.Background() == nullptr)
                cell.Background(SolidColorBrush{ Microsoft::UI::Colors::Transparent() });
            cell.Tapped([this, from](IInspectable const&, Input::TappedRoutedEventArgs const&)
            {
                /* clic dans une case vide : nouvel événement ce jour-là, à 9 h */
                BeginEventFromSelection(from + 9 * 3600, from + 10 * 3600);
            });

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
            ToolTipService::SetToolTip(number, box_value(Tr(L"Afficher ce jour")));
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

                hstring color = ::EvolutionWin::CategoryStore::EventColorHex(ev);
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
                chip.Tapped([](IInspectable const&, Input::TappedRoutedEventArgs const& args) { args.Handled(true); });
                chip.DoubleTapped([this, calendar_uid, uid](IInspectable const&, Input::DoubleTappedRoutedEventArgs const& args)
                {
                    args.Handled(true);
                    OpenCalendarEvent(calendar_uid, uid);
                });
                AttachEventContextMenu(chip, ev);
                EnableEventExportDrag(chip, ev);
                content.Children().Append(chip);
            }

            if (hidden > 0)
            {
                TextBlock more;
                more.Text(Fmt(hidden > 1 ? Tr(L"+{0} autres") : Tr(L"+{0} autre"), { to_hstring(hidden) }));
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

    /** @brief Largeur de la barre d'en-tête du calendrier modifiée : réorganise les commandes si elles ne tiennent plus. */
    void MainWindow::OnCalendarHeaderSizeChanged(IInspectable const&, SizeChangedEventArgs const&)
    {
        UpdateCalendarHeaderLayout();
    }

    /**
     * @brief Place les commandes du calendrier (navigation à gauche, période / filtre / export à droite) sur une
     *        seule ligne quand la largeur le permet, sinon sur deux lignes pour qu'elles ne se chevauchent pas.
     */
    void MainWindow::UpdateCalendarHeaderLayout()
    {
        auto grid = CalendarHeaderGrid();
        auto left = CalendarHeaderLeft();
        auto right = CalendarHeaderRight();
        if (grid.ActualWidth() <= 0)
            return;

        /* largeur voulue sans contrainte : Measure () borné par la grille la réduirait */
        Windows::Foundation::Size unbounded{ (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)() };
        left.Measure(unbounded);
        right.Measure(unbounded);
        double needed = left.DesiredSize().Width + right.DesiredSize().Width + 24;
        bool stacked = needed > grid.ActualWidth();
        if (stacked == m_calendar_header_stacked && grid.RowDefinitions().Size() == (stacked ? 2u : 0u))
            return;
        m_calendar_header_stacked = stacked;

        grid.RowDefinitions().Clear();
        if (stacked)
        {
            for (int row = 0; row < 2; ++row)
            {
                RowDefinition definition;
                definition.Height(GridLengthHelper::Auto());
                grid.RowDefinitions().Append(definition);
            }
            grid.RowSpacing(8);
            Grid::SetRow(left, 0);
            Grid::SetRow(right, 1);
            right.HorizontalAlignment(HorizontalAlignment::Left);
        }
        else
        {
            grid.RowSpacing(0);
            Grid::SetRow(left, 0);
            Grid::SetRow(right, 0);
            right.HorizontalAlignment(HorizontalAlignment::Right);
        }
    }

    /* Largeur minimale de la colonne des calendriers et place laissée à la grille. */
    static constexpr double CalendarSidebarMinWidth = 200;
    static constexpr double CalendarGridMinWidth = 420;

    /**
     * @brief Rétablit la largeur de la colonne des calendriers mémorisée (réglage layout.calendar-sidebar).
     *        Sans réglage, la colonne garde sa largeur automatique.
     */
    void MainWindow::RestoreCalendarSidebarWidth()
    {
        if (m_calendar_sidebar_restored)
            return;
        m_calendar_sidebar_restored = true;

        double width = ::EvolutionWin::Settings::Instance().GetNumber(L"layout.calendar-sidebar", 0);
        if (width >= CalendarSidebarMinWidth)
            CalendarSidebarColumn().Width(GridLengthHelper::FromPixels(width));
    }

    /** @brief Début du déplacement du séparateur : capture du pointeur et largeur de départ. */
    void MainWindow::OnCalendarSplitterPointerPressed(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        auto splitter = sender.as<UIElement>();
        if (!args.GetCurrentPoint(splitter).Properties().IsLeftButtonPressed() || !splitter.CapturePointer(args.Pointer()))
            return;

        m_calendar_dragging = true;
        m_calendar_drag_origin = args.GetCurrentPoint(CalendarViewPane()).Position().X;
        m_calendar_drag_start = CalendarSidebarColumn().ActualWidth();
    }

    /** @brief Pendant le glissement : nouvelle largeur de la colonne, bornée pour laisser la place à la grille. */
    void MainWindow::OnCalendarSplitterPointerMoved(IInspectable const&, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_calendar_dragging)
            return;

        double max_width = (std::max)(CalendarSidebarMinWidth, CalendarViewPane().ActualWidth() - CalendarGridMinWidth - 72);
        double x = args.GetCurrentPoint(CalendarViewPane()).Position().X;
        double width = std::clamp(m_calendar_drag_start + x - m_calendar_drag_origin, CalendarSidebarMinWidth, max_width);
        CalendarSidebarColumn().Width(GridLengthHelper::FromPixels(std::round(width)));
    }

    /** @brief Fin du glissement : mémorise la largeur. */
    void MainWindow::OnCalendarSplitterPointerReleased(IInspectable const& sender, Input::PointerRoutedEventArgs const& args)
    {
        if (!m_calendar_dragging)
            return;
        m_calendar_dragging = false;
        sender.as<UIElement>().ReleasePointerCapture(args.Pointer());
        ::EvolutionWin::Settings::Instance().SetNumber(L"layout.calendar-sidebar", CalendarSidebarColumn().ActualWidth());
    }

    /** @brief Capture perdue : le glissement s'arrête. */
    void MainWindow::OnCalendarSplitterPointerCaptureLost(IInspectable const&, Input::PointerRoutedEventArgs const&)
    {
        m_calendar_dragging = false;
    }

    /** @brief Double-clic : retour à la largeur automatique de la colonne. */
    void MainWindow::OnCalendarSplitterDoubleTapped(IInspectable const&, Input::DoubleTappedRoutedEventArgs const&)
    {
        CalendarSidebarColumn().Width(GridLengthHelper::Auto());
        ::EvolutionWin::Settings::Instance().SetNumber(L"layout.calendar-sidebar", 0);
    }

    /**
     * @brief Modifie le pas horaire de la grille du calendrier (15, 30 ou 60 minutes).
     * @param sender Élément de menu émetteur (MenuFlyoutItem).
     * @param args Arguments de clic.
     */
    void MainWindow::OnCalendarTimeScaleClick(
        IInspectable const& sender,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        if (auto item = sender.try_as<MenuFlyoutItem>())
        {
            hstring scale = unbox_value_or<hstring>(item.Tag(), L"30");
            ::EvolutionWin::Settings::Instance().SetString(L"calendar.time-scale", scale);
            ApplyCalendarPreferences();
            ShowStatus(Fmt(Tr(L"Échelle de temps modifiée : {0} minutes."), { scale }), InfoBarSeverity::Informational);
        }
    }

    /**
     * @brief Bascule l'affichage des calendriers entre mode superposé et côte à côte.
     * @param sender Bouton à bascule émetteur.
     * @param args Arguments d'événement.
     */
    void MainWindow::OnCalendarLayoutModeToggled(
        IInspectable const&,
        [[maybe_unused]] RoutedEventArgs const& args)
    {
        if (auto toggle = RibbonCalendarSideBySideToggle())
        {
            bool is_side_by_side = toggle.IsChecked() && toggle.IsChecked().Value();
            ::EvolutionWin::Settings::Instance().SetString(
                L"calendar.layout-mode", is_side_by_side ? L"side-by-side" : L"overlay");
            ApplyCalendarPreferences();
            ShowStatus(is_side_by_side ? Tr(L"Affichage des calendriers côte à côte.") : Tr(L"Affichage des calendriers superposés."), InfoBarSeverity::Informational);
        }
    }
}
