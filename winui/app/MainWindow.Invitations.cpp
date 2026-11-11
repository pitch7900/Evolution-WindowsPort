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

        InviteBar().Severity(SeverityOf(state.tone));
        InviteBar().IsOpen(true);
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
