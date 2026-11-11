#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Helpers.h"
#include "Invitations.h"

/* Invitations iCalendar : bandeau d'action du volet de lecture (Accepter, Provisoire,
 * Refuser, Retirer de l'agenda), mise à jour de l'agenda et réponse à l'organisateur
 * par le moteur (evo_core_respond_invitation). Même mécanisme de bandeau que les accusés
 * de lecture (MainWindow.Receipts.cpp). */

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;

namespace winrt::EvolutionWin::implementation
{
    using ::EvolutionWin::Helpers::Fmt;
    using ::EvolutionWin::Helpers::Tr;
    using ::EvolutionWin::Helpers::UiIsEnglish;

    namespace
    {
        /**
         * @brief Traduit un libellé français des phrases de Invitations.h vers la langue de l'interface.
         * @param fr Libellé français (clé de traduction).
         * @return Libellé traduit (le français si aucune traduction n'existe).
         */
        std::wstring TranslateInvitationText(wchar_t const* fr)
        {
            return std::wstring{ Tr(fr) };
        }

        /**
         * @brief Indique si deux instants tombent le même jour local.
         * @param a Premier horodatage Unix (secondes).
         * @param b Second horodatage Unix (secondes).
         * @return true si même jour civil local.
         */
        bool SameLocalDay(double a, double b)
        {
            time_t ta = static_cast<time_t>(a), tb = static_cast<time_t>(b);
            struct tm tma{}, tmb{};
            localtime_s(&tma, &ta);
            localtime_s(&tmb, &tb);
            return tma.tm_year == tmb.tm_year && tma.tm_yday == tmb.tm_yday;
        }

        /**
         * @brief Texte « quand » d'une réunion : date longue et heures locales.
         * @param start Début (horodatage Unix, secondes).
         * @param end Fin (secondes) ; exclusive pour une journée entière.
         * @param all_day true pour une réunion sur la journée entière.
         * @return Date et heures localisées, vide si le début est inconnu.
         */
        std::wstring FormatWhen(double start, double end, bool all_day)
        {
            if (start <= 0)
                return {};

            Windows::Globalization::DateTimeFormatting::DateTimeFormatter day_format{ L"longdate" };
            auto day_of = [&](double t) { return day_format.Format(winrt::clock::from_time_t(static_cast<time_t>(t))); };

            if (all_day)
            {
                double last = end > start ? end - 1 : start;
                if (SameLocalDay(start, last))
                    return std::wstring{ day_of(start) };
                return std::wstring{ day_of(start) } + L" – " + std::wstring{ day_of(last) };
            }

            if (end <= start || SameLocalDay(start, end))
            {
                std::wstring text = std::wstring{ day_of(start) } + L", " + std::wstring{ ::EvolutionWin::Helpers::FormatTime(start) };
                if (end > start)
                    text += L" – " + std::wstring{ ::EvolutionWin::Helpers::FormatTime(end) };
                return text;
            }
            return std::wstring{ ::EvolutionWin::Helpers::FormatDate(start) } + L" – " + std::wstring{ ::EvolutionWin::Helpers::FormatDate(end) };
        }

        /** @brief Teinte du bandeau WinUI correspondant à une teinte logique. */
        InfoBarSeverity SeverityOf(::EvolutionWin::Invitations::Tone tone)
        {
            using ::EvolutionWin::Invitations::Tone;
            return tone == Tone::Warning ? InfoBarSeverity::Warning
                : tone == Tone::Success ? InfoBarSeverity::Success : InfoBarSeverity::Informational;
        }
    }

    /** @brief Referme le bandeau d'invitation. */
    void MainWindow::HideInvitationBar()
    {
        InviteBar().IsOpen(false);
    }

    /**
     * @brief Met à jour le bandeau d'invitation pour le message affiché.
     *
     * - REQUEST : titre, date et heure locales, lieu, organisateur, boutons Accepter /
     *   Provisoire / Refuser (le bouton de la réponse déjà donnée est désactivé) et case
     *   « envoyer une réponse à l'organisateur » ;
     * - CANCEL : réunion annulée, bouton « Retirer de l'agenda » si elle y figure encore ;
     * - REPLY : « X a accepté la réunion ».
     * @param message Objet JSON renvoyé par evo_core_get_message (membre « invitation »).
     */
    void MainWindow::UpdateInvitationBar(JsonObject const& message)
    {
        using namespace ::EvolutionWin::Invitations;

        HideInvitationBar();
        if (!message.HasKey(L"invitation"))
            return;

        JsonObject inv = message.GetNamedObject(L"invitation", JsonObject{});
        Kind kind = ParseKind(std::wstring_view{ inv.GetNamedString(L"method", L"") });

        JsonObject organizer = inv.GetNamedObject(L"organizer", JsonObject{});
        hstring organizer_email = organizer.GetNamedString(L"email", L"");
        std::wstring organizer_name = DisplayName(std::wstring_view{ organizer.GetNamedString(L"name", L"") },
            std::wstring_view{ organizer_email });
        bool is_organizer = inv.GetNamedBoolean(L"is_organizer", false);
        bool in_calendar = inv.GetNamedBoolean(L"already_in_calendar", false);
        /* FALSE tant que la recherche dans l'agenda (asynchrone) n'est pas terminée */
        bool calendar_checked = inv.GetNamedBoolean(L"calendar_checked", true);
        Answer mine = ParseAnswer(std::wstring_view{ inv.GetNamedString(L"my_status", L"") });

        BannerState state = Decide(kind, is_organizer, !organizer_email.empty(), in_calendar);

        /* l'état dans l'agenda se complète en tâche de fond, sans retarder l'affichage du message */
        if (!calendar_checked)
            CheckInvitationInCalendar();

        if (!state.visible)
            return;

        hstring summary = inv.GetNamedString(L"summary", L"");
        if (summary.empty())
            summary = Tr(L"(Sans titre)", L"(No title)");
        hstring label = kind == Kind::Cancel ? Tr(L"Réunion annulée", L"Meeting cancelled")
            : kind == Kind::Reply ? Tr(L"Réponse à une invitation", L"Invitation response")
            : Tr(L"Invitation à une réunion", L"Meeting invitation");
        InviteBar().Title(label + L" : " + summary);

        /* quand / où / organisateur / participants */
        std::wstring details;
        std::wstring when = FormatWhen(inv.GetNamedNumber(L"start", 0), inv.GetNamedNumber(L"end", 0),
            inv.GetNamedBoolean(L"all_day", false));
        if (!when.empty())
            details += std::wstring{ inv.GetNamedBoolean(L"recurring", false)
                ? Fmt(Tr(L"Quand : {0} (périodique)", L"When: {0} (recurring)"), { hstring{ when } })
                : Fmt(Tr(L"Quand : {0}", L"When: {0}"), { hstring{ when } }) } + L"\n";
        hstring location = inv.GetNamedString(L"location", L"");
        if (!location.empty())
            details += std::wstring{ Fmt(Tr(L"Lieu : {0}", L"Where: {0}"), { location }) } + L"\n";
        if (!organizer_name.empty())
            details += std::wstring{ Fmt(Tr(L"Organisateur : {0}", L"Organizer: {0}"), { hstring{ organizer_name } }) } + L"\n";
        uint32_t attendee_count = inv.GetNamedArray(L"attendees", JsonArray{}).Size();
        if (kind == Kind::Request && attendee_count > 0)
            details += std::wstring{ Fmt(Tr(L"Participants : {0}", L"Attendees: {0}"), { to_hstring(attendee_count) }) } + L"\n";
        if (!details.empty())
            details.pop_back();
        InviteDetails().Text(hstring{ details });
        InviteDetails().Visibility(details.empty() ? Visibility::Collapsed : Visibility::Visible);

        /* ligne d'état : réponse donnée, annulation, ou réponse d'un participant */
        std::wstring status_text;
        if (kind == Kind::Reply)
        {
            for (auto const& value : inv.GetNamedArray(L"attendees", JsonArray{}))
            {
                JsonObject who = value.GetObject();
                Answer answer = ParseAnswer(std::wstring_view{ who.GetNamedString(L"partstat", L"") });
                status_text = DescribeReply(DisplayName(std::wstring_view{ who.GetNamedString(L"name", L"") },
                    std::wstring_view{ who.GetNamedString(L"email", L"") }), answer, UiIsEnglish(), &TranslateInvitationText);
                state.tone = answer == Answer::Declined ? Tone::Warning
                    : answer == Answer::NeedsAction ? Tone::Informational : Tone::Success;
                break;
            }
        }
        else
        {
            status_text = DescribeState(kind, mine, in_calendar, is_organizer, UiIsEnglish(), &TranslateInvitationText);
            if (kind == Kind::Request && mine == Answer::Declined)
                state.tone = Tone::Warning;
            else if (kind == Kind::Request && (mine == Answer::Accepted || mine == Answer::Tentative))
                state.tone = Tone::Success;
        }
        /* réponse déjà donnée : « Acceptée le 11.11.2026 09:16. » comme dans Outlook (date lue dans l'agenda Microsoft 365) */
        double response_time = inv.GetNamedNumber(L"response_time", 0);
        if (kind != Kind::Reply && response_time > 0 && (mine == Answer::Accepted || mine == Answer::Tentative || mine == Answer::Declined))
        {
            hstring when_text = ::EvolutionWin::Helpers::FormatDate(response_time);
            status_text = std::wstring{ mine == Answer::Accepted ? Fmt(Tr(L"Acceptée le {0}."), { when_text })
                : mine == Answer::Tentative ? Fmt(Tr(L"Provisoire le {0}."), { when_text })
                : Fmt(Tr(L"Refusée le {0}."), { when_text }) };
        }

        /* état dans l'agenda encore inconnu : on n'affirme rien (« Merci de répondre » serait faux pour une invitation déjà acceptée) */
        if (!calendar_checked && kind != Kind::Reply)
            status_text.clear();
        InviteState().Text(hstring{ status_text });
        InviteState().Visibility(status_text.empty() ? Visibility::Collapsed : Visibility::Visible);

        /* boutons */
        auto show = [](Button const& button, bool visible, bool enabled)
        {
            button.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
            button.IsEnabled(enabled);
        };
        InviteAcceptButton().Content(box_value(Tr(L"Accepter", L"Accept")));
        InviteTentativeButton().Content(box_value(Tr(L"Provisoire", L"Tentative")));
        InviteDeclineButton().Content(box_value(Tr(L"Refuser", L"Decline")));
        InviteRemoveButton().Content(box_value(Tr(L"Retirer de l'agenda", L"Remove from calendar")));
        show(InviteAcceptButton(), state.showResponseButtons, mine != Answer::Accepted);
        show(InviteTentativeButton(), state.showResponseButtons, mine != Answer::Tentative);
        show(InviteDeclineButton(), state.showResponseButtons, mine != Answer::Declined);
        show(InviteRemoveButton(), state.showRemoveButton, true);

        InviteSendReplyCheck().Content(box_value(Tr(L"Envoyer une réponse à l'organisateur", L"Send a reply to the organizer")));
        InviteSendReplyCheck().IsChecked(true);
        InviteSendReplyCheck().Visibility(state.showSendChoice ? Visibility::Visible : Visibility::Collapsed);

        RenderInvitationDayGrid(inv);

        InviteBar().Severity(SeverityOf(state.tone));
        InviteBar().IsOpen(true);
    }

    /**
     * @brief Dessine, dans le bandeau d'invitation, la mini-grille de la journée autour de la réunion
     *        (comme Outlook) : colonne des heures, la réunion proposée en couleur d'accentuation, et les autres
     *        événements de l'agenda (voies côte à côte quand ils se chevauchent) pour voir les conflits.
     *        Masquée tant que les événements de l'agenda ne sont pas connus (Microsoft 365 seulement).
     * @param inv Objet « invitation » du message (start, end, summary, day_events).
     */
    void MainWindow::RenderInvitationDayGrid(JsonObject const& inv)
    {
        auto grid = InviteDayGrid();
        grid.Children().Clear();
        grid.ColumnDefinitions().Clear();
        grid.RowDefinitions().Clear();
        InviteDayHost().Visibility(Visibility::Collapsed);

        double start = inv.GetNamedNumber(L"start", 0);
        double end = inv.GetNamedNumber(L"end", 0);
        if (inv.GetNamedBoolean(L"all_day", false) || start <= 0 || end <= start || !inv.HasKey(L"day_events"))
            return;

        constexpr double kPxPerHour = 44.0;

        /* fenêtre : une heure avant, une heure et demie après, au quart d'heure près, 3 h au moins */
        double window_start = std::floor((start - 3600.0) / 1800.0) * 1800.0;
        double window_end = std::ceil((end + 5400.0) / 1800.0) * 1800.0;
        if (window_end - window_start < 3 * 3600.0)
            window_end = window_start + 3 * 3600.0;
        double total_height = (window_end - window_start) / 3600.0 * kPxPerHour;

        struct Item { double start; double end; hstring subject; hstring show_as; bool self; };
        std::vector<Item> items;
        items.push_back({ start, end, inv.GetNamedString(L"summary", L""), L"busy", true });

        for (auto const& value : inv.GetNamedArray(L"day_events", JsonArray{}))
        {
            auto event = value.GetObject();
            double event_start = event.GetNamedNumber(L"start", 0);
            double event_end = event.GetNamedNumber(L"end", 0);
            if (event.GetNamedBoolean(L"self", false) || event.GetNamedBoolean(L"all_day", false) ||
                event_end <= window_start || event_start >= window_end || event_end <= event_start)
                continue;
            items.push_back({ event_start, event_end, event.GetNamedString(L"subject", L""), event.GetNamedString(L"show_as", L"busy"), false });
        }
        std::stable_sort(items.begin() + 1, items.end(), [](Item const& a, Item const& b) { return a.start < b.start; });

        /* voies : la réunion proposée d'abord, puis chaque événement dans la première voie libre */
        std::vector<double> lane_end;
        std::vector<size_t> lane_of(items.size(), 0);
        for (size_t i = 0; i < items.size(); ++i)
        {
            size_t lane = 0;
            while (lane < lane_end.size() && lane_end[lane] > items[i].start)
                ++lane;
            if (lane == lane_end.size())
                lane_end.push_back(0);
            lane_end[lane] = items[i].end;
            lane_of[i] = lane;
        }

        ColumnDefinition label_column, lanes_column;
        label_column.Width(GridLengthHelper::FromPixels(52));
        lanes_column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        grid.ColumnDefinitions().Append(label_column);
        grid.ColumnDefinitions().Append(lanes_column);
        grid.Height(total_height);

        auto resources = Application::Current().Resources();
        auto brush = [&](wchar_t const* key) { return resources.Lookup(box_value(key)).as<Microsoft::UI::Xaml::Media::Brush>(); };

        /* heures et traits */
        for (double hour = std::ceil(window_start / 3600.0) * 3600.0; hour < window_end; hour += 3600.0)
        {
            double y = (hour - window_start) / 3600.0 * kPxPerHour;

            TextBlock label;
            label.Text(::EvolutionWin::Helpers::FormatTime(hour));
            label.FontSize(11);
            label.Opacity(0.7);
            label.Margin(ThicknessHelper::FromLengths(8, y - 8, 0, 0));
            label.VerticalAlignment(VerticalAlignment::Top);
            Grid::SetColumn(label, 0);
            grid.Children().Append(label);

            Border line;
            line.Height(1);
            line.VerticalAlignment(VerticalAlignment::Top);
            line.Margin(ThicknessHelper::FromLengths(0, y, 0, 0));
            line.Background(brush(L"DividerStrokeColorDefaultBrush"));
            Grid::SetColumn(line, 1);
            grid.Children().Append(line);
        }

        /* événements, une colonne par voie */
        Grid lanes;
        Grid::SetColumn(lanes, 1);
        for (size_t lane = 0; lane < lane_end.size(); ++lane)
        {
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            lanes.ColumnDefinitions().Append(column);
        }

        for (size_t i = 0; i < items.size(); ++i)
        {
            auto const& item = items[i];
            double top = (std::max)(item.start, window_start);
            double bottom = (std::min)(item.end, window_end);
            double y = (top - window_start) / 3600.0 * kPxPerHour;
            double height = (std::max)(20.0, (bottom - top) / 3600.0 * kPxPerHour - 1);

            Border block;
            block.VerticalAlignment(VerticalAlignment::Top);
            block.Margin(ThicknessHelper::FromLengths(2, y, 2, 0));
            block.Height(height);
            block.CornerRadius(CornerRadiusHelper::FromUniformRadius(3));
            block.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));
            block.BorderThickness(ThicknessHelper::FromUniformLength(1));

            TextBlock text;
            text.FontSize(11);
            text.TextTrimming(TextTrimming::CharacterEllipsis);
            text.TextWrapping(TextWrapping::NoWrap);
            hstring subject = item.subject.empty() ? hstring{ L"…" } : item.subject;

            if (item.self)
            {
                /* la réunion proposée : couleur d'accentuation */
                block.Background(brush(L"AccentFillColorDefaultBrush"));
                block.BorderBrush(brush(L"AccentFillColorDefaultBrush"));
                text.Foreground(brush(L"TextOnAccentFillColorPrimaryBrush"));
                text.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
            }
            else
            {
                bool free_time = item.show_as == L"free";
                block.Background(free_time ? brush(L"LayerFillColorDefaultBrush") : brush(L"CardBackgroundFillColorSecondaryBrush"));
                block.BorderBrush(brush(L"CardStrokeColorDefaultBrush"));
                text.Opacity(free_time ? 0.7 : 1.0);
            }
            text.Text(subject);
            block.Child(text);
            Grid::SetColumn(block, static_cast<int32_t>(lane_of[i]));
            lanes.Children().Append(block);
        }
        grid.Children().Append(lanes);

        InviteDayHost().Visibility(Visibility::Visible);
    }

    /**
     * @brief Cherche l'événement de l'invitation affichée dans l'agenda (en tâche de fond) puis complète
     *        le bandeau : déjà dans l'agenda, réponse déjà donnée (acceptée, provisoire, refusée).
     */
    void MainWindow::CheckInvitationInCalendar()
    {
        if (!m_current_message || !m_current_message.HasKey(L"invitation") || m_current_message_account.empty())
            return;

        hstring event_uid = m_current_message.GetNamedObject(L"invitation", JsonObject{}).GetNamedString(L"uid", L"");
        if (event_uid.empty())
            return;

        hstring message_uid = m_current_message_uid;
        m_core->FindInvitationEvent(m_current_message_account, event_uid,
            [this, message_uid](hstring const& json, hstring const& error)
        {
            if (!m_current_message || m_current_message_uid != message_uid || !m_current_message.HasKey(L"invitation"))
                return;   /* un autre message est affiché entre-temps */

            JsonObject inv = m_current_message.GetNamedObject(L"invitation", JsonObject{});
            JsonObject result;
            if (error.empty() && JsonObject::TryParse(json, result) && result.GetNamedBoolean(L"found", false))
            {
                inv.SetNamedValue(L"already_in_calendar", JsonValue::CreateBooleanValue(true));
                inv.SetNamedValue(L"calendar_sequence", JsonValue::CreateNumberValue(result.GetNamedNumber(L"sequence", 0)));
                inv.SetNamedValue(L"response_time", JsonValue::CreateNumberValue(result.GetNamedNumber(L"response_time", 0)));
                if (result.HasKey(L"events"))
                    inv.SetNamedValue(L"day_events", result.GetNamedArray(L"events"));
                /* une réponse donnée depuis l'application (étiquette du message) prime sur l'agenda */
                if (inv.GetNamedString(L"my_status", L"needs-action") == L"needs-action")
                    inv.SetNamedValue(L"my_status", JsonValue::CreateStringValue(result.GetNamedString(L"my_status", L"needs-action")));
            }
            /* en cas d'échec ou d'événement introuvable : état par défaut, le bandeau ne reste pas en attente */
            inv.SetNamedValue(L"calendar_checked", JsonValue::CreateBooleanValue(true));
            UpdateInvitationBar(m_current_message);
        });
    }

    /**
     * @brief Répond à l'invitation du message affiché.
     *
     * Le moteur met à jour l'agenda, envoie la réponse iTIP (si la case est cochée) et mémorise
     * la réponse ; le bandeau est ensuite rafraîchi ainsi que la vue Calendrier.
     * @param response "accepted", "tentative", "declined" ou "remove" (réunion annulée).
     */
    void MainWindow::RespondToInvitation(hstring const& response)
    {
        using namespace ::EvolutionWin::Invitations;

        if (!m_current_message || m_current_message_account.empty() || m_current_message_folder.empty())
            return;

        hstring uid = m_current_message_uid;
        bool send = response != L"remove" && InviteSendReplyCheck().Visibility() == Visibility::Visible
            && InviteSendReplyCheck().IsChecked().Value();

        InviteAcceptButton().IsEnabled(false);
        InviteTentativeButton().IsEnabled(false);
        InviteDeclineButton().IsEnabled(false);
        InviteRemoveButton().IsEnabled(false);

        m_core->RespondInvitation(m_current_message_account, m_current_message_folder, uid, response, send,
            [this, uid, response](hstring const& json, hstring const& error)
        {
            bool same_message = m_current_message && m_current_message_uid == uid;

            if (!error.empty())
            {
                /* rien n'est mémorisé : le bandeau est rétabli pour une nouvelle tentative */
                if (same_message)
                    UpdateInvitationBar(m_current_message);
                ShowStatus(Fmt(Tr(L"Réponse à l'invitation impossible : {0}", L"Could not respond to the invitation: {0}"), { error }),
                    InfoBarSeverity::Warning);
                return;
            }

            JsonObject result = JsonObject::Parse(json);
            hstring calendar = result.GetNamedString(L"calendar", L"");
            hstring calendar_error = result.GetNamedString(L"calendar_error", L"");
            bool sent = result.GetNamedBoolean(L"sent", false);

            if (same_message && m_current_message.HasKey(L"invitation"))
            {
                JsonObject inv = m_current_message.GetNamedObject(L"invitation", JsonObject{});
                if (response != L"remove")
                    inv.SetNamedValue(L"my_status", JsonValue::CreateStringValue(response));
                if (calendar == L"added" || calendar == L"updated" || calendar == L"kept")
                    inv.SetNamedValue(L"already_in_calendar", JsonValue::CreateBooleanValue(true));
                else if (calendar == L"removed")
                    inv.SetNamedValue(L"already_in_calendar", JsonValue::CreateBooleanValue(false));
                UpdateInvitationBar(m_current_message);
            }

            ShowStatus(hstring{ DescribeResult(std::wstring_view{ response }, std::wstring_view{ calendar }, sent, UiIsEnglish(), &TranslateInvitationText) },
                InfoBarSeverity::Success);
            if (!calendar_error.empty())
                ShowStatus(Fmt(Tr(L"Agenda non mis à jour : {0}", L"Calendar not updated: {0}"), { calendar_error }), InfoBarSeverity::Warning);

            /* rafraîchit la vue Calendrier (nouvel événement, retrait, réponse) */
            if (calendar != L"unchanged")
                LoadEvents();
        });
    }

    /** @brief Bouton « Accepter » du bandeau d'invitation. */
    void MainWindow::OnInviteAcceptClick(IInspectable const&, RoutedEventArgs const&)
    {
        RespondToInvitation(L"accepted");
    }

    /** @brief Bouton « Provisoire » du bandeau d'invitation. */
    void MainWindow::OnInviteTentativeClick(IInspectable const&, RoutedEventArgs const&)
    {
        RespondToInvitation(L"tentative");
    }

    /** @brief Bouton « Refuser » du bandeau d'invitation. */
    void MainWindow::OnInviteDeclineClick(IInspectable const&, RoutedEventArgs const&)
    {
        RespondToInvitation(L"declined");
    }

    /** @brief Bouton « Retirer de l'agenda » d'une réunion annulée. */
    void MainWindow::OnInviteRemoveClick(IInspectable const&, RoutedEventArgs const&)
    {
        RespondToInvitation(L"remove");
    }
}
