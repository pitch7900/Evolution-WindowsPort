#pragma once

/* Invitations iCalendar (iTIP) : fonctions pures (sans dépendance à WinRT) qui décident
 * de l'aspect du bandeau du volet de lecture et rédigent ses textes. Partagées par
 * l'application (MainWindow.Invitations.cpp) et par les tests unitaires. */

#include <cstddef>
#include <string>
#include <string_view>

namespace EvolutionWin::Invitations
{
    /** @brief Nature de l'invitation, d'après la méthode iTIP du message. */
    enum class Kind
    {
        None,     ///< pas d'invitation exploitable (méthode absente ou non prise en charge)
        Request,  ///< invitation ou mise à jour (METHOD:REQUEST) : à accepter ou refuser
        Cancel,   ///< annulation de la réunion (METHOD:CANCEL)
        Reply     ///< réponse d'un participant (METHOD:REPLY) reçue par l'organisateur
    };

    /** @brief Réponse d'un participant (PARTSTAT). */
    enum class Answer
    {
        NeedsAction,  ///< pas encore de réponse
        Accepted,
        Tentative,
        Declined
    };

    /** @brief Teinte du bandeau. */
    enum class Tone
    {
        Informational,
        Success,
        Warning
    };

    /** @brief Aspect du bandeau d'invitation. */
    struct BannerState
    {
        bool visible = false;          ///< afficher le bandeau
        bool showResponseButtons = false;  ///< Accepter / Provisoire / Refuser
        bool showSendChoice = false;   ///< case « envoyer une réponse à l'organisateur »
        bool showRemoveButton = false; ///< « Retirer de l'agenda » (réunion annulée)
        Tone tone = Tone::Informational;
    };

    /**
     * @brief Lit la méthode iTIP renvoyée par le moteur.
     * @param method "REQUEST", "CANCEL" ou "REPLY" (insensible à la casse).
     * @return Nature de l'invitation, Kind::None pour toute autre valeur (PUBLISH, COUNTER...).
     */
    inline Kind ParseKind(std::wstring_view method)
    {
        auto equals = [&](std::wstring_view name)
        {
            if (method.size() != name.size())
                return false;
            for (size_t i = 0; i < name.size(); ++i)
            {
                wchar_t c = method[i];
                if (c >= L'a' && c <= L'z')
                    c = static_cast<wchar_t>(c - L'a' + L'A');
                if (c != name[i])
                    return false;
            }
            return true;
        };

        if (equals(L"REQUEST"))
            return Kind::Request;
        if (equals(L"CANCEL"))
            return Kind::Cancel;
        if (equals(L"REPLY"))
            return Kind::Reply;
        return Kind::None;
    }

    /**
     * @brief Lit une réponse renvoyée par le moteur.
     * @param value "accepted", "tentative", "declined" ; toute autre valeur donne NeedsAction.
     * @return Réponse correspondante.
     */
    inline Answer ParseAnswer(std::wstring_view value)
    {
        if (value == L"accepted")
            return Answer::Accepted;
        if (value == L"tentative")
            return Answer::Tentative;
        if (value == L"declined")
            return Answer::Declined;
        return Answer::NeedsAction;
    }

    /**
     * @brief Valeur de réponse attendue par evo_core_respond_invitation.
     * @param answer Réponse choisie.
     * @return "accepted", "tentative", "declined" ou "needs-action".
     */
    inline wchar_t const* AnswerToResponse(Answer answer)
    {
        switch (answer)
        {
        case Answer::Accepted: return L"accepted";
        case Answer::Tentative: return L"tentative";
        case Answer::Declined: return L"declined";
        default: return L"needs-action";
        }
    }

    /**
     * @brief Décide de l'aspect du bandeau.
     * @param kind Nature de l'invitation.
     * @param is_organizer true si l'utilisateur est l'organisateur de la réunion.
     * @param can_reply true si l'organisateur est connu (une réponse peut lui être envoyée).
     * @param in_calendar true si la réunion figure déjà dans un agenda de l'utilisateur.
     * @return Aspect du bandeau.
     */
    inline BannerState Decide(Kind kind, bool is_organizer, bool can_reply, bool in_calendar)
    {
        BannerState state;

        switch (kind)
        {
        case Kind::Request:
            state.visible = true;
            state.showResponseButtons = !is_organizer;
            state.showSendChoice = !is_organizer && can_reply;
            break;
        case Kind::Cancel:
            state.visible = true;
            state.tone = Tone::Warning;
            state.showRemoveButton = in_calendar && !is_organizer;
            break;
        case Kind::Reply:
            state.visible = true;
            break;
        default:
            break;
        }
        return state;
    }

    /**
     * @brief Texte de la ligne d'état (réponse déjà donnée, réunion annulée...).
     * @param kind Nature de l'invitation.
     * @param mine Réponse actuelle de l'utilisateur.
     * @param in_calendar true si la réunion figure dans un agenda.
     * @param is_organizer true si l'utilisateur est l'organisateur.
     * @param english true pour l'anglais, false pour le français.
     * @return Phrase localisée, vide s'il n'y a rien à dire.
     */
    inline std::wstring DescribeState(Kind kind, Answer mine, bool in_calendar, bool is_organizer, bool english)
    {
        if (kind == Kind::Cancel)
            return english ? (in_calendar ? L"This meeting was cancelled. It is still in your calendar."
                                          : L"This meeting was cancelled.")
                           : (in_calendar ? L"Cette réunion a été annulée. Elle figure encore dans votre agenda."
                                          : L"Cette réunion a été annulée.");
        if (kind != Kind::Request)
            return {};
        if (is_organizer)
            return english ? L"You are the organizer of this meeting." : L"Vous êtes l'organisateur de cette réunion.";

        switch (mine)
        {
        case Answer::Accepted:
            return english ? L"You accepted this invitation." : L"Vous avez accepté cette invitation.";
        case Answer::Tentative:
            return english ? L"You tentatively accepted this invitation." : L"Vous avez répondu « Provisoire » à cette invitation.";
        case Answer::Declined:
            return english ? L"You declined this invitation." : L"Vous avez refusé cette invitation.";
        default:
            return in_calendar ? (english ? L"This meeting is already in your calendar. Please respond."
                                          : L"Cette réunion figure déjà dans votre agenda. Merci de répondre.")
                               : (english ? L"Please respond to this invitation." : L"Merci de répondre à cette invitation.");
        }
    }

    /**
     * @brief Phrase d'une réponse de participant reçue (« Marie a accepté la réunion »).
     * @param who Nom ou adresse du participant ; vide si inconnu.
     * @param answer Réponse donnée.
     * @param english true pour l'anglais, false pour le français.
     * @return Phrase localisée.
     */
    inline std::wstring DescribeReply(std::wstring_view who, Answer answer, bool english)
    {
        std::wstring name = who.empty() ? std::wstring{ english ? L"A participant" : L"Un participant" } : std::wstring{ who };

        switch (answer)
        {
        case Answer::Accepted: return name + (english ? L" accepted the meeting." : L" a accepté la réunion.");
        case Answer::Tentative: return name + (english ? L" tentatively accepted the meeting." : L" a répondu « Provisoire » à la réunion.");
        case Answer::Declined: return name + (english ? L" declined the meeting." : L" a refusé la réunion.");
        default: return name + (english ? L" replied to the meeting." : L" a répondu à la réunion.");
        }
    }

    /**
     * @brief Message de confirmation après une réponse à une invitation.
     * @param response Réponse envoyée au moteur : "accepted", "tentative", "declined" ou "remove".
     * @param calendar Action effectuée sur l'agenda : "added", "updated", "kept", "removed", "unchanged" ou "skipped".
     * @param sent true si une réponse a été envoyée à l'organisateur.
     * @param english true pour l'anglais, false pour le français.
     * @return Phrase localisée.
     */
    inline std::wstring DescribeResult(std::wstring_view response, std::wstring_view calendar, bool sent, bool english)
    {
        std::wstring text;

        if (response == L"remove")
            text = english ? L"Meeting removed from your calendar." : L"Réunion retirée de votre agenda.";
        else if (response == L"declined")
            text = english ? L"Invitation declined." : L"Invitation refusée.";
        else if (response == L"tentative")
            text = english ? L"Invitation tentatively accepted." : L"Invitation acceptée provisoirement.";
        else
            text = english ? L"Invitation accepted." : L"Invitation acceptée.";

        if (calendar == L"added")
            text += english ? L" Added to your calendar." : L" Ajoutée à votre agenda.";
        else if (calendar == L"updated" || calendar == L"kept")
            text += english ? L" Calendar updated." : L" Agenda mis à jour.";
        else if (calendar == L"removed" && response != L"remove")
            text += english ? L" Removed from your calendar." : L" Retirée de votre agenda.";
        else if (calendar == L"skipped")
            text += english ? L" (A single occurrence of a series is not added to the calendar.)"
                            : L" (Une occurrence isolée d'une série n'est pas ajoutée à l'agenda.)";

        if (sent)
            text += english ? L" Reply sent to the organizer." : L" Réponse envoyée à l'organisateur.";
        return text;
    }

    /**
     * @brief Nom à afficher pour une personne : son nom, à défaut son adresse.
     * @param name Nom affiché (peut être vide).
     * @param email Adresse (peut être vide).
     * @return Nom ou adresse.
     */
    inline std::wstring DisplayName(std::wstring_view name, std::wstring_view email)
    {
        return std::wstring{ name.empty() ? email : name };
    }
}
