#pragma once

/* Invitations iCalendar (iTIP) : fonctions pures (sans dépendance à WinRT) qui décident
 * de l'aspect du bandeau du volet de lecture et rédigent ses textes. Partagées par
 * l'application (MainWindow.Invitations.cpp) et par les tests unitaires. */

#include <cstddef>
#include <string>
#include <string_view>

namespace EvolutionWin::Invitations
{
    /** @brief Traducteur d'un libellé français (nullptr : le français est conservé) ; fourni par l'application. */
    using Translator = std::wstring (*)(wchar_t const* fr);

    /**
     * @brief Choisit le libellé selon la langue : anglais, traduction du français, ou français.
     * @param english true pour l'anglais.
     * @param translate Traducteur du français (peut être nullptr).
     * @param fr Libellé français (clé de traduction).
     * @param en Libellé anglais.
     * @return Le libellé dans la langue demandée.
     */
    inline std::wstring PickText(bool english, Translator translate, wchar_t const* fr, wchar_t const* en)
    {
        if (english)
            return en;
        return translate ? translate(fr) : std::wstring{ fr };
    }

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
     * @param english true pour l'anglais, false pour le français (ou la langue du traducteur).
     * @param translate Traducteur du français vers la langue de l'interface (nullptr : français).
     * @return Phrase localisée, vide s'il n'y a rien à dire.
     */
    inline std::wstring DescribeState(Kind kind, Answer mine, bool in_calendar, bool is_organizer, bool english,
        Translator translate = nullptr)
    {
        auto Tr = [&](wchar_t const* fr, wchar_t const* en) { return PickText(english, translate, fr, en); };

        if (kind == Kind::Cancel)
            return in_calendar ? Tr(L"Cette réunion a été annulée. Elle figure encore dans votre agenda.",
                                    L"This meeting was cancelled. It is still in your calendar.")
                               : Tr(L"Cette réunion a été annulée.", L"This meeting was cancelled.");
        if (kind != Kind::Request)
            return {};
        if (is_organizer)
            return Tr(L"Vous êtes l'organisateur de cette réunion.", L"You are the organizer of this meeting.");

        switch (mine)
        {
        case Answer::Accepted:
            return Tr(L"Vous avez accepté cette invitation.", L"You accepted this invitation.");
        case Answer::Tentative:
            return Tr(L"Vous avez répondu « Provisoire » à cette invitation.", L"You tentatively accepted this invitation.");
        case Answer::Declined:
            return Tr(L"Vous avez refusé cette invitation.", L"You declined this invitation.");
        default:
            return in_calendar ? Tr(L"Cette réunion figure déjà dans votre agenda. Merci de répondre.",
                                    L"This meeting is already in your calendar. Please respond.")
                               : Tr(L"Merci de répondre à cette invitation.", L"Please respond to this invitation.");
        }
    }

    /**
     * @brief Phrase d'une réponse de participant reçue (« Marie a accepté la réunion »).
     * @param who Nom ou adresse du participant ; vide si inconnu.
     * @param answer Réponse donnée.
     * @param english true pour l'anglais, false pour le français (ou la langue du traducteur).
     * @param translate Traducteur du français vers la langue de l'interface (nullptr : français).
     * @return Phrase localisée.
     */
    inline std::wstring DescribeReply(std::wstring_view who, Answer answer, bool english, Translator translate = nullptr)
    {
        auto Tr = [&](wchar_t const* fr, wchar_t const* en) { return PickText(english, translate, fr, en); };
        std::wstring name = who.empty() ? Tr(L"Un participant", L"A participant") : std::wstring{ who };
        std::wstring text;

        switch (answer)
        {
        case Answer::Accepted: text = Tr(L"{0} a accepté la réunion.", L"{0} accepted the meeting."); break;
        case Answer::Tentative: text = Tr(L"{0} a répondu « Provisoire » à la réunion.", L"{0} tentatively accepted the meeting."); break;
        case Answer::Declined: text = Tr(L"{0} a refusé la réunion.", L"{0} declined the meeting."); break;
        default: text = Tr(L"{0} a répondu à la réunion.", L"{0} replied to the meeting."); break;
        }
        size_t marker = text.find(L"{0}");
        if (marker != std::wstring::npos)
            text.replace(marker, 3, name);
        return text;
    }

    /**
     * @brief Message de confirmation après une réponse à une invitation.
     * @param response Réponse envoyée au moteur : "accepted", "tentative", "declined" ou "remove".
     * @param calendar Action effectuée sur l'agenda : "added", "updated", "kept", "removed", "unchanged" ou "skipped".
     * @param sent true si une réponse a été envoyée à l'organisateur.
     * @param english true pour l'anglais, false pour le français (ou la langue du traducteur).
     * @param translate Traducteur du français vers la langue de l'interface (nullptr : français).
     * @return Phrase localisée.
     */
    inline std::wstring DescribeResult(std::wstring_view response, std::wstring_view calendar, bool sent, bool english,
        Translator translate = nullptr)
    {
        auto Tr = [&](wchar_t const* fr, wchar_t const* en) { return PickText(english, translate, fr, en); };
        std::wstring text;

        if (response == L"remove")
            text = Tr(L"Réunion retirée de votre agenda.", L"Meeting removed from your calendar.");
        else if (response == L"declined")
            text = Tr(L"Invitation refusée.", L"Invitation declined.");
        else if (response == L"tentative")
            text = Tr(L"Invitation acceptée provisoirement.", L"Invitation tentatively accepted.");
        else
            text = Tr(L"Invitation acceptée.", L"Invitation accepted.");

        if (calendar == L"added")
            text += L" " + Tr(L"Ajoutée à votre agenda.", L"Added to your calendar.");
        else if (calendar == L"updated" || calendar == L"kept")
            text += L" " + Tr(L"Agenda mis à jour.", L"Calendar updated.");
        else if (calendar == L"removed" && response != L"remove")
            text += L" " + Tr(L"Retirée de votre agenda.", L"Removed from your calendar.");
        else if (calendar == L"skipped")
            text += L" " + Tr(L"(Une occurrence isolée d'une série n'est pas ajoutée à l'agenda.)",
                              L"(A single occurrence of a series is not added to the calendar.)");

        if (sent)
            text += L" " + Tr(L"Réponse envoyée à l'organisateur.", L"Reply sent to the organizer.");
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
