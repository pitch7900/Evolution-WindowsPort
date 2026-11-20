#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "PeopleCalendars.h"

#include <ctime>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Microsoft::UI::Xaml::Media;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

using namespace ::EvolutionWin::Helpers;

/**
 * @file MainWindow.EventMove.cpp
 * @brief Calendrier : plage sélectionnée dans la grille (persistante) et son menu contextuel, copier / coller
 * d'un événement à l'endroit du sélecteur, et déplacement d'un événement par glisser dans la grille
 * (pas de 15 minutes).
 */

namespace
{
    /**
     * @brief Décale une heure murale « AAAAMMJJTHHMMSS » d'un nombre de secondes.
     * @param wall Heure murale du moteur (dans le fuseau de l'événement).
     * @param delta Décalage en secondes (positif ou négatif).
     * @return Heure murale décalée, ou la valeur d'origine si elle est illisible.
     */
    hstring ShiftWall(hstring const& wall, int64_t delta)
    {
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (swscanf_s(wall.c_str(), L"%4d%2d%2dT%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6)
            return wall;
        struct tm parts{};
        parts.tm_year = y - 1900;
        parts.tm_mon = mo - 1;
        parts.tm_mday = d;
        parts.tm_hour = h;
        parts.tm_min = mi;
        parts.tm_sec = s;
        __time64_t seconds = _mkgmtime64(&parts);
        if (seconds < 0)
            return wall;
        seconds += delta;
        struct tm shifted{};
        if (_gmtime64_s(&shifted, &seconds) != 0)
            return wall;
        wchar_t buffer[24];
        swprintf_s(buffer, L"%04d%02d%02dT%02d%02d%02d", shifted.tm_year + 1900, shifted.tm_mon + 1, shifted.tm_mday,
            shifted.tm_hour, shifted.tm_min, shifted.tm_sec);
        return buffer;
    }
}

namespace winrt::EvolutionWin::implementation
{
    /**
     * @brief Sélectionne une plage de la grille : elle reste affichée jusqu'à ce qu'on en choisisse une autre.
     * @param start_unix Début de la plage (secondes Unix).
     * @param end_unix Fin de la plage (secondes Unix).
     */
    void MainWindow::SelectCalendarSlot(int64_t start_unix, int64_t end_unix, hstring const& calendar)
    {
        m_slot_start = start_unix;
        m_slot_end = end_unix;
        m_slot_calendar = calendar;
        m_selected_event_key = L"";
        m_selected_event = nullptr;
        UpdateEventSelectionVisuals();
        UpdateSlotSelectionVisuals();
    }

    /**
     * @brief Redessine la plage sélectionnée sur chaque jour affiché (sans reconstruire la grille).
     */
    void MainWindow::UpdateSlotSelectionVisuals()
    {
        bool has_selection = m_slot_start > 0 && m_slot_end > m_slot_start;
        for (auto const& visual : m_slot_visuals)
        {
            if (!has_selection || visual.calendar != m_slot_calendar || m_slot_end <= visual.day_start || m_slot_start >= visual.day_end)
            {
                visual.border.Visibility(Visibility::Collapsed);
                if (visual.top_grip)
                    visual.top_grip.Visibility(Visibility::Collapsed);
                if (visual.bottom_grip)
                    visual.bottom_grip.Visibility(Visibility::Collapsed);
                continue;
            }
            int64_t from = (std::max)(m_slot_start, visual.day_start) - visual.day_start;
            int64_t to = (std::min)(m_slot_end, visual.day_end) - visual.day_start;
            visual.border.Margin(ThicknessHelper::FromLengths(2, from * visual.hour_height / 3600, 4, 0));
            visual.border.Height((std::max)(4.0, (to - from) * visual.hour_height / 3600));
            visual.border.Visibility(Visibility::Visible);

            /* poignées de 8 pixels sur les bords haut et bas de la plage (le bord n'est étirable que s'il est dans ce jour) */
            if (visual.top_grip)
            {
                bool own_edge = m_slot_start >= visual.day_start;
                visual.top_grip.as<FrameworkElement>().Margin(ThicknessHelper::FromLengths(2, from * visual.hour_height / 3600 - 3, 4, 0));
                visual.top_grip.Visibility(own_edge ? Visibility::Visible : Visibility::Collapsed);
            }
            if (visual.bottom_grip)
            {
                bool own_edge = m_slot_end <= visual.day_end;
                visual.bottom_grip.as<FrameworkElement>().Margin(ThicknessHelper::FromLengths(2, to * visual.hour_height / 3600 - 5, 4, 0));
                visual.bottom_grip.Visibility(own_edge ? Visibility::Visible : Visibility::Collapsed);
            }
        }
    }

    /**
     * @brief Menu du clic droit sur la plage sélectionnée (comme Outlook) : nouveau rendez-vous, journée entière,
     *        réunion Teams, coller un événement copié et retour à aujourd'hui.
     * @param anchor Élément sur lequel le menu s'ouvre (couche du jour).
     * @param position Position du clic dans cet élément.
     */
    void MainWindow::ShowCalendarSlotMenu(UIElement const& anchor, Point position)
    {
        if (m_slot_end <= m_slot_start)
            return;

        MenuFlyout flyout;
        int64_t start = m_slot_start;
        int64_t end = m_slot_end;
        hstring pane = m_slot_calendar;   /* calendrier du panneau cliqué (vide : mode superposé) */

        auto add_item = [&flyout](hstring const& text, wchar_t const* glyph, bool enabled, auto&& handler)
        {
            MenuFlyoutItem item;
            item.Text(text);
            if (glyph)
            {
                FontIcon icon;
                icon.Glyph(glyph);
                item.Icon(icon);
            }
            item.IsEnabled(enabled);
            item.Click([handler](IInspectable const&, RoutedEventArgs const&) { handler(); });
            flyout.Items().Append(item);
        };

        add_item(Tr(L"Nouveau rendez-vous"), L"\xE787", true, [this, start, end, pane]() { BeginEventFromSelection(start, end, pane); });

        add_item(Tr(L"Nouvel événement d'une journée entière"), nullptr, true, [this, start, pane]()
        {
            time_t raw = static_cast<time_t>(start);
            struct tm local{};
            localtime_s(&local, &raw);
            local.tm_hour = 0;
            local.tm_min = 0;
            local.tm_sec = 0;
            local.tm_isdst = -1;
            int64_t midnight = static_cast<int64_t>(mktime(&local));
            m_new_event_all_day = true;
            BeginEventFromSelection(midnight, midnight + 86400, pane);
        });

        if (!TeamsCalendarUid().empty())
        {
            add_item(Tr(L"Nouvelle réunion Teams"), L"\xE716", true, [this, start, end]()
            {
                m_new_event_start = start;
                m_new_event_end = end;
                ShowEventDialogAsync(nullptr, L"", false, true);
            });
        }

        flyout.Items().Append(MenuFlyoutSeparator{});
        add_item(Tr(L"Coller"), L"\xE77F", !m_event_clipboard_uid.empty(), [this, start, pane]() { PasteCalendarEvent(start, pane); });

        flyout.Items().Append(MenuFlyoutSeparator{});
        add_item(Tr(L"Aujourd'hui"), L"\xE787", true, [this]() { OnRibbonTodayClick(nullptr, nullptr); });

        FlyoutShowOptions options;
        options.Position(position);
        flyout.ShowAt(anchor, options);
    }

    /**
     * @brief Copie un événement : il sera collé (dupliqué) à l'endroit de la plage sélectionnée.
     * @param event Événement au format JSON de la liste du moteur (calendar_uid, uid).
     */
    void MainWindow::CopyCalendarEvent(JsonObject const& event)
    {
        m_event_clipboard_calendar = event.GetNamedString(L"calendar_uid", L"");
        m_event_clipboard_uid = event.GetNamedString(L"uid", L"");
        ShowStatus(Fmt(Tr(L"Événement « {0} » copié : clic droit sur une plage du calendrier, puis « Coller »."),
            { event.GetNamedString(L"summary", L"") }), InfoBarSeverity::Informational);
    }

    /**
     * @brief Colle l'événement copié : un nouvel événement, de même durée et de mêmes propriétés, commence à
     *        l'heure donnée. Les participants ne sont pas recopiés (aucune invitation n'est envoyée).
     * @param start_unix Début du nouvel événement (secondes Unix).
     */
    void MainWindow::PasteCalendarEvent(int64_t start_unix, hstring const& calendar_hint)
    {
        if (m_event_clipboard_uid.empty() || !m_core)
            return;

        m_core->GetEvent(m_event_clipboard_calendar, m_event_clipboard_uid, [this, start_unix, calendar_hint](hstring const& json, hstring const& error)
        {
            JsonObject source;
            if (!error.empty() || !JsonObject::TryParse(json, source))
            {
                ShowStatus(Fmt(Tr(L"Copie de l'événement : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }

            bool all_day = source.GetNamedBoolean(L"all_day", false);
            int64_t old_start = static_cast<int64_t>(source.GetNamedNumber(L"start", 0));
            int64_t old_end = static_cast<int64_t>(source.GetNamedNumber(L"end", 0));
            int64_t duration = (std::max<int64_t>)(old_end - old_start, all_day ? 86400 : 900);
            int64_t new_start = start_unix;
            if (all_day)
            {
                time_t raw = static_cast<time_t>(start_unix);
                struct tm local{};
                localtime_s(&local, &raw);
                local.tm_hour = 0;
                local.tm_min = 0;
                local.tm_sec = 0;
                local.tm_isdst = -1;
                new_start = static_cast<int64_t>(mktime(&local));
            }

            JsonObject payload;
            for (auto const* name : { L"summary", L"location", L"description", L"show_as", L"importance", L"rrule" })
                if (source.HasKey(name))
                    payload.SetNamedValue(name, JsonValue::CreateStringValue(source.GetNamedString(name, L"")));
            if (source.HasKey(L"categories"))
                payload.SetNamedValue(L"categories", source.GetNamedValue(L"categories"));
            payload.SetNamedValue(L"private", JsonValue::CreateBooleanValue(source.GetNamedBoolean(L"private", false)));
            payload.SetNamedValue(L"reminder_minutes", JsonValue::CreateNumberValue(source.GetNamedNumber(L"reminder_minutes", -1)));
            payload.SetNamedValue(L"all_day", JsonValue::CreateBooleanValue(all_day));
            payload.SetNamedValue(L"start", JsonValue::CreateNumberValue(static_cast<double>(new_start)));
            payload.SetNamedValue(L"end", JsonValue::CreateNumberValue(static_cast<double>(new_start + duration)));

            /* calendrier du panneau où l'on colle (côte à côte), sinon agenda d'origine, sauf s'il est en lecture seule :
               agenda actif ; un calendrier de personne (lecture seule) n'est jamais une destination */
            hstring calendar = m_event_clipboard_calendar;
            if (!calendar_hint.empty() && !::EvolutionWin::People::IsPersonUid(std::wstring_view{ calendar_hint }))
                calendar = calendar_hint;
            else if (source.GetNamedBoolean(L"readonly", false))
                calendar = ActiveCalendarUid();
            if (!calendar.empty())
                payload.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(calendar));

            m_core->CreateEvent(payload.Stringify(), [this](hstring const&, hstring const& create_error)
            {
                if (!create_error.empty())
                {
                    ShowStatus(Fmt(Tr(L"Collage de l'événement : {0}"), { create_error }), InfoBarSeverity::Error);
                    return;
                }
                ShowStatus(Tr(L"Événement collé."), InfoBarSeverity::Success);
                LoadEvents();
            });
        });
    }

    /**
     * @brief Déplace un événement : il commence à la nouvelle heure et garde sa durée.
     * @param calendar_uid Agenda de l'événement.
     * @param uid Identifiant de l'événement.
     * @param new_start Nouveau début (secondes Unix).
     */
    void MainWindow::MoveCalendarEvent(hstring const& calendar_uid, hstring const& uid, int64_t new_start)
    {
        ChangeCalendarEventTimes(calendar_uid, uid, new_start, 0, 0);
    }

    /**
     * @brief Étire un événement par son bord haut ou bas : le début et / ou la fin se décalent.
     * @param calendar_uid Agenda de l'événement.
     * @param uid Identifiant de l'événement.
     * @param start_delta Décalage du début en secondes (bord haut), 0 si inchangé.
     * @param end_delta Décalage de la fin en secondes (bord bas), 0 si inchangée.
     */
    void MainWindow::ResizeCalendarEvent(hstring const& calendar_uid, hstring const& uid, int64_t start_delta, int64_t end_delta)
    {
        ChangeCalendarEventTimes(calendar_uid, uid, 0, start_delta, end_delta);
    }

    /**
     * @brief Change les heures d'un événement (déplacement ou étirement) puis recharge le calendrier. Les événements
     *        périodiques, en lecture seule ou sur la journée entière ne sont pas modifiés ainsi.
     * @param calendar_uid Agenda de l'événement.
     * @param uid Identifiant de l'événement.
     * @param new_start Déplacement : nouveau début (la durée est conservée) ; 0 pour un étirement.
     * @param start_delta Étirement : décalage du début en secondes.
     * @param end_delta Étirement : décalage de la fin en secondes.
     */
    void MainWindow::ChangeCalendarEventTimes(hstring const& calendar_uid, hstring const& uid, int64_t new_start,
        int64_t start_delta, int64_t end_delta)
    {
        if (!m_core)
            return;

        m_core->GetEvent(calendar_uid, uid, [this, calendar_uid, uid, new_start, start_delta, end_delta](hstring const& json, hstring const& error)
        {
            JsonObject detail;
            if (!error.empty() || !JsonObject::TryParse(json, detail))
            {
                ShowStatus(Fmt(Tr(L"Déplacement de l'événement : {0}"), { error }), InfoBarSeverity::Error);
                LoadEvents();
                return;
            }
            if (detail.GetNamedBoolean(L"recurring", false))
            {
                ShowStatus(Tr(L"Un événement périodique ne se déplace pas par glisser : ouvrez-le pour modifier sa récurrence."),
                    InfoBarSeverity::Warning);
                LoadEvents();
                return;
            }
            if (detail.GetNamedBoolean(L"readonly", false))
            {
                ShowStatus(Tr(L"Cet agenda est en lecture seule : l'événement ne peut pas être déplacé."), InfoBarSeverity::Warning);
                LoadEvents();
                return;
            }
            if (detail.GetNamedBoolean(L"all_day", false))
            {
                LoadEvents();
                return;
            }

            int64_t old_start = static_cast<int64_t>(detail.GetNamedNumber(L"start", 0));
            int64_t old_end = static_cast<int64_t>(detail.GetNamedNumber(L"end", 0));
            int64_t shift_start = start_delta;
            int64_t shift_end = end_delta;
            if (new_start > 0)
            {
                shift_start = new_start - old_start;
                shift_end = shift_start;
            }
            if (shift_start == 0 && shift_end == 0)
                return;
            if ((old_end + shift_end) - (old_start + shift_start) < 900)
            {
                LoadEvents();   /* durée minimale d'un quart d'heure */
                return;
            }

            JsonObject request;
            request.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(calendar_uid));
            request.SetNamedValue(L"uid", JsonValue::CreateStringValue(uid));
            request.SetNamedValue(L"all_day", JsonValue::CreateBooleanValue(false));
            request.SetNamedValue(L"start", JsonValue::CreateNumberValue(static_cast<double>(old_start + shift_start)));
            request.SetNamedValue(L"end", JsonValue::CreateNumberValue(static_cast<double>(old_end + shift_end)));

            /* événement daté dans un fuseau : les heures murales de ce fuseau suivent le décalage */
            hstring zone = detail.GetNamedString(L"timezone", L"");
            hstring start_wall = detail.GetNamedString(L"start_wall", L"");
            hstring end_wall = detail.GetNamedString(L"end_wall", L"");
            if (!zone.empty() && !start_wall.empty() && !end_wall.empty())
            {
                request.SetNamedValue(L"timezone", JsonValue::CreateStringValue(zone));
                request.SetNamedValue(L"start_wall", JsonValue::CreateStringValue(ShiftWall(start_wall, shift_start)));
                request.SetNamedValue(L"end_wall", JsonValue::CreateStringValue(ShiftWall(end_wall, shift_end)));
            }

            m_core->ModifyEvent(request.Stringify(), [this](hstring const&, hstring const& modify_error)
            {
                if (!modify_error.empty())
                    ShowStatus(Fmt(Tr(L"Déplacement de l'événement : {0}"), { modify_error }), InfoBarSeverity::Error);
                LoadEvents();
            });
        });
    }

    /**
     * @brief Sélectionne un événement de la grille (contour d'accent) ; la plage sélectionnée est abandonnée.
     * @param event Événement au format JSON de la liste du moteur.
     */
    void MainWindow::SelectCalendarEvent(JsonObject const& event)
    {
        m_selected_event = event;
        m_selected_event_key = event.GetNamedString(L"calendar_uid", L"") + L"\x1F" + event.GetNamedString(L"uid", L"") + L"\x1F" +
            to_hstring(static_cast<int64_t>(event.GetNamedNumber(L"start", 0)));
        m_slot_start = 0;
        m_slot_end = 0;
        UpdateSlotSelectionVisuals();
        UpdateEventSelectionVisuals();
    }

    /**
     * @brief Redessine la sélection des événements de la grille : contour d'accent sur l'événement sélectionné, poignées
     *        d'étirement visibles sur lui seul, aspect d'origine pour les autres.
     */
    void MainWindow::UpdateEventSelectionVisuals()
    {
        auto accent = Application::Current().Resources().Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Microsoft::UI::Xaml::Media::Brush>();
        auto highlight = Application::Current().Resources().Lookup(box_value(L"SubtleFillColorSecondaryBrush")).as<Microsoft::UI::Xaml::Media::Brush>();
        for (auto const& visual : m_event_visuals)
        {
            bool selected = !m_selected_event_key.empty() && visual.key == m_selected_event_key;
            if (selected)
            {
                visual.border.BorderBrush(accent);
                visual.border.BorderThickness(ThicknessHelper::FromUniformLength(2));
                if (!visual.background)
                    visual.border.Background(highlight);
            }
            else
            {
                visual.border.BorderBrush(visual.brush);
                visual.border.BorderThickness(visual.thickness);
                visual.border.Background(visual.background);
            }
            if (visual.top_grip)
                visual.top_grip.Visibility(selected ? Visibility::Visible : Visibility::Collapsed);
            if (visual.bottom_grip)
                visual.bottom_grip.Visibility(selected ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    /**
     * @brief Rend un élément de la grille sélectionnable : un clic (ou un clic droit) le sélectionne.
     * @param element Bloc ou pastille de l'événement.
     * @param event Événement au format JSON de la liste du moteur.
     * @param top_grip Poignée d'étirement du haut (nullptr si l'événement n'en a pas).
     * @param bottom_grip Poignée d'étirement du bas (nullptr si l'événement n'en a pas).
     */
    void MainWindow::RegisterCalendarEvent(UIElement const& element, JsonObject const& event, UIElement const& top_grip, UIElement const& bottom_grip)
    {
        element.Tapped([this, event](IInspectable const&, Input::TappedRoutedEventArgs const& args)
        {
            args.Handled(true);
            SelectCalendarEvent(event);
        });
        element.RightTapped([this, event](IInspectable const&, Input::RightTappedRoutedEventArgs const&)
        {
            SelectCalendarEvent(event);   /* le menu contextuel de l'événement s'ouvre ensuite */
        });

        if (auto border = element.try_as<Border>())
        {
            m_event_visuals.push_back(EventVisual{ border,
                event.GetNamedString(L"calendar_uid", L"") + L"\x1F" + event.GetNamedString(L"uid", L"") + L"\x1F" +
                    to_hstring(static_cast<int64_t>(event.GetNamedNumber(L"start", 0))),
                border.BorderThickness(), border.BorderBrush(), border.Background(), top_grip, bottom_grip });
        }
    }

    /**
     * @brief Ctrl+C dans le calendrier : copie l'événement sélectionné.
     */
    void MainWindow::OnCalendarCopyAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        /* dans une zone de saisie, le raccourci appartient au texte */
        if (Content() && Content().XamlRoot())
            if (Input::FocusManager::GetFocusedElement(Content().XamlRoot()).try_as<TextBox>())
                return;
        if (CalendarGridHost().Visibility() != Visibility::Visible || !m_selected_event || m_selected_event_key.empty())
            return;
        args.Handled(true);
        CopyCalendarEvent(m_selected_event);
    }

    /**
     * @brief Ctrl+V dans le calendrier : colle l'événement copié à l'endroit de la plage sélectionnée.
     */
    void MainWindow::OnCalendarPasteAccelerator(Input::KeyboardAccelerator const&, Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        if (Content() && Content().XamlRoot())
            if (Input::FocusManager::GetFocusedElement(Content().XamlRoot()).try_as<TextBox>())
                return;
        if (CalendarGridHost().Visibility() != Visibility::Visible || m_event_clipboard_uid.empty())
            return;
        args.Handled(true);
        if (m_slot_end > m_slot_start)
            PasteCalendarEvent(m_slot_start, m_slot_calendar);
        else
            ShowStatus(Tr(L"Sélectionnez d'abord une plage du calendrier où coller l'événement."), InfoBarSeverity::Informational);
    }
}
