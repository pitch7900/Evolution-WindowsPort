#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"

#include <ctime>
#include <string>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
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
    void MainWindow::SelectCalendarSlot(int64_t start_unix, int64_t end_unix)
    {
        m_slot_start = start_unix;
        m_slot_end = end_unix;
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
            if (!has_selection || m_slot_end <= visual.day_start || m_slot_start >= visual.day_end)
            {
                visual.border.Visibility(Visibility::Collapsed);
                continue;
            }
            int64_t from = (std::max)(m_slot_start, visual.day_start) - visual.day_start;
            int64_t to = (std::min)(m_slot_end, visual.day_end) - visual.day_start;
            visual.border.Margin(ThicknessHelper::FromLengths(2, from * visual.hour_height / 3600, 4, 0));
            visual.border.Height((std::max)(4.0, (to - from) * visual.hour_height / 3600));
            visual.border.Visibility(Visibility::Visible);
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

        add_item(Tr(L"Nouveau rendez-vous"), L"\xE787", true, [this, start, end]() { BeginEventFromSelection(start, end); });

        add_item(Tr(L"Nouvel événement d'une journée entière"), nullptr, true, [this, start]()
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
            BeginEventFromSelection(midnight, midnight + 86400);
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
        add_item(Tr(L"Coller"), L"\xE77F", !m_event_clipboard_uid.empty(), [this, start]() { PasteCalendarEvent(start); });

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
    void MainWindow::PasteCalendarEvent(int64_t start_unix)
    {
        if (m_event_clipboard_uid.empty() || !m_core)
            return;

        m_core->GetEvent(m_event_clipboard_calendar, m_event_clipboard_uid, [this, start_unix](hstring const& json, hstring const& error)
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

            /* agenda d'origine, sauf s'il est en lecture seule : agenda actif */
            hstring calendar = source.GetNamedBoolean(L"readonly", false) ? ActiveCalendarUid() : m_event_clipboard_calendar;
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
     * @brief Déplace un événement : il commence à la nouvelle heure et garde sa durée. Les événements périodiques,
     *        en lecture seule ou sur la journée entière ne sont pas déplacés par glisser.
     * @param calendar_uid Agenda de l'événement.
     * @param uid Identifiant de l'événement.
     * @param new_start Nouveau début (secondes Unix).
     */
    void MainWindow::MoveCalendarEvent(hstring const& calendar_uid, hstring const& uid, int64_t new_start)
    {
        if (!m_core)
            return;

        m_core->GetEvent(calendar_uid, uid, [this, calendar_uid, uid, new_start](hstring const& json, hstring const& error)
        {
            JsonObject detail;
            if (!error.empty() || !JsonObject::TryParse(json, detail))
            {
                ShowStatus(Fmt(Tr(L"Déplacement de l'événement : {0}"), { error }), InfoBarSeverity::Error);
                return;
            }
            if (detail.GetNamedBoolean(L"recurring", false))
            {
                ShowStatus(Tr(L"Un événement périodique ne se déplace pas par glisser : ouvrez-le pour modifier sa récurrence."),
                    InfoBarSeverity::Warning);
                return;
            }
            if (detail.GetNamedBoolean(L"readonly", false))
            {
                ShowStatus(Tr(L"Cet agenda est en lecture seule : l'événement ne peut pas être déplacé."), InfoBarSeverity::Warning);
                return;
            }
            if (detail.GetNamedBoolean(L"all_day", false))
                return;

            int64_t old_start = static_cast<int64_t>(detail.GetNamedNumber(L"start", 0));
            int64_t old_end = static_cast<int64_t>(detail.GetNamedNumber(L"end", 0));
            int64_t delta = new_start - old_start;
            if (delta == 0)
                return;

            JsonObject request;
            request.SetNamedValue(L"calendar_uid", JsonValue::CreateStringValue(calendar_uid));
            request.SetNamedValue(L"uid", JsonValue::CreateStringValue(uid));
            request.SetNamedValue(L"all_day", JsonValue::CreateBooleanValue(false));
            request.SetNamedValue(L"start", JsonValue::CreateNumberValue(static_cast<double>(old_start + delta)));
            request.SetNamedValue(L"end", JsonValue::CreateNumberValue(static_cast<double>(old_end + delta)));

            /* événement daté dans un fuseau : les heures murales de ce fuseau suivent le décalage */
            hstring zone = detail.GetNamedString(L"timezone", L"");
            hstring start_wall = detail.GetNamedString(L"start_wall", L"");
            hstring end_wall = detail.GetNamedString(L"end_wall", L"");
            if (!zone.empty() && !start_wall.empty() && !end_wall.empty())
            {
                request.SetNamedValue(L"timezone", JsonValue::CreateStringValue(zone));
                request.SetNamedValue(L"start_wall", JsonValue::CreateStringValue(ShiftWall(start_wall, delta)));
                request.SetNamedValue(L"end_wall", JsonValue::CreateStringValue(ShiftWall(end_wall, delta)));
            }

            m_core->ModifyEvent(request.Stringify(), [this, calendar_uid](hstring const&, hstring const& modify_error)
            {
                if (!modify_error.empty())
                {
                    ShowStatus(Fmt(Tr(L"Déplacement de l'événement : {0}"), { modify_error }), InfoBarSeverity::Error);
                    return;
                }
                LoadEvents();
            });
        });
    }
}
