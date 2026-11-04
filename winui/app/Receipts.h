#pragma once

/* Accusés de lecture (MDN, RFC 8098) : fonctions pures (sans dépendance à WinRT)
 * partagées par l'application et par les tests unitaires. */

#include <cstddef>
#include <string>
#include <string_view>

namespace EvolutionWin::Receipts
{
    /** @brief Réglage par défaut du traitement des demandes d'accusé de lecture. */
    enum class Policy
    {
        Ask,     ///< toujours demander (par défaut)
        Never,   ///< ne jamais envoyer (aucune proposition)
        Always   ///< toujours envoyer (réglage explicite)
    };

    /** @brief Décision prise à l'ouverture d'un message demandant un accusé. */
    enum class Action
    {
        None,      ///< rien à faire (pas de demande, déjà traité ou réglage « jamais »)
        Ask,       ///< afficher le bandeau Envoyer / Ne pas envoyer
        SendAuto   ///< envoyer sans demander
    };

    /**
     * @brief Lit le réglage enregistré.
     * @param value Valeur stockée ("ask", "never", "always"), toute autre valeur donne « demander ».
     * @return Politique correspondante.
     */
    inline Policy ParsePolicy(std::wstring_view value)
    {
        if (value == L"never")
            return Policy::Never;
        if (value == L"always")
            return Policy::Always;
        return Policy::Ask;
    }

    /**
     * @brief Texte à enregistrer pour une politique.
     * @param policy Politique.
     * @return "ask", "never" ou "always".
     */
    inline std::wstring_view PolicyToString(Policy policy)
    {
        return policy == Policy::Never ? L"never" : policy == Policy::Always ? L"always" : L"ask";
    }

    /**
     * @brief Extrait l'adresse électronique d'un champ d'adresse (« Nom <a@b> » ou « a@b »).
     * @param field Valeur d'en-tête.
     * @return Adresse en minuscules, ou chaîne vide si aucune adresse n'est trouvée.
     */
    inline std::wstring AddressOf(std::wstring_view field)
    {
        std::wstring_view address = field;
        auto open = field.find(L'<');
        if (open != std::wstring_view::npos)
        {
            auto close = field.find(L'>', open);
            if (close == std::wstring_view::npos)
                return {};
            address = field.substr(open + 1, close - open - 1);
        }

        std::wstring out;
        for (wchar_t c : address)
        {
            if (c == L' ' || c == L'\t' || c == L'"')
                continue;
            out.push_back(c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c);
        }
        return out.find(L'@') == std::wstring::npos ? std::wstring{} : out;
    }

    /**
     * @brief Domaine d'une adresse électronique.
     * @param address Adresse (de préférence issue de AddressOf).
     * @return Partie après le dernier « @ », ou chaîne vide.
     */
    inline std::wstring DomainOf(std::wstring_view address)
    {
        auto at = address.rfind(L'@');
        return at == std::wstring_view::npos ? std::wstring{} : std::wstring{ address.substr(at + 1) };
    }

    /**
     * @brief Indique si l'adresse de l'accusé est suspecte (domaine différent de l'expéditeur).
     * @param receipt_to En-tête Disposition-Notification-To / Return-Receipt-To.
     * @param from En-tête From du message.
     * @return true si les domaines diffèrent ou si l'un d'eux est illisible.
     */
    inline bool IsReceiptAddressSuspicious(std::wstring_view receipt_to, std::wstring_view from)
    {
        auto a = DomainOf(AddressOf(receipt_to));
        auto b = DomainOf(AddressOf(from));
        return a.empty() || b.empty() || a != b;
    }

    /**
     * @brief Décide quoi faire d'une demande d'accusé de lecture à l'ouverture d'un message.
     *
     * Aucun envoi automatique sans réglage explicite « toujours envoyer », et même
     * dans ce cas la demande dont l'adresse n'appartient pas au domaine de
     * l'expéditeur est soumise à confirmation.
     * @param policy Réglage de l'utilisateur.
     * @param receipt_to En-tête de demande d'accusé ("" si aucune).
     * @param from En-tête From du message.
     * @param already_processed true si l'accusé a déjà été envoyé ou refusé ($MDNSent).
     * @return Action à entreprendre.
     */
    inline Action Decide(Policy policy, std::wstring_view receipt_to, std::wstring_view from, bool already_processed)
    {
        if (already_processed || AddressOf(receipt_to).empty() || policy == Policy::Never)
            return Action::None;
        if (policy == Policy::Always && !IsReceiptAddressSuspicious(receipt_to, from))
            return Action::SendAuto;
        return Action::Ask;
    }

    /** @brief Champs d'un bloc message/disposition-notification reçu (RFC 8098 §3.2). */
    struct Mdn
    {
        bool valid{ false };                 ///< au moins Final-Recipient ou Disposition lu
        std::wstring final_recipient;        ///< adresse de celui qui a lu (sans « rfc822; »)
        std::wstring original_message_id;    ///< Message-ID du message d'origine
        std::wstring disposition_type;       ///< displayed, deleted, dispatched, processed, denied, failed
    };

    namespace detail
    {
        inline std::wstring Trim(std::wstring_view text)
        {
            size_t b = 0, e = text.size();
            while (b < e && (text[b] == L' ' || text[b] == L'\t' || text[b] == L'\r'))
                ++b;
            while (e > b && (text[e - 1] == L' ' || text[e - 1] == L'\t' || text[e - 1] == L'\r'))
                --e;
            return std::wstring{ text.substr(b, e - b) };
        }

        inline std::wstring Lower(std::wstring_view text)
        {
            std::wstring out{ text };
            for (auto& c : out)
                if (c >= L'A' && c <= L'Z')
                    c = static_cast<wchar_t>(c + 32);
            return out;
        }
    }

    /**
     * @brief Analyse le texte d'un bloc message/disposition-notification.
     *
     * Les lignes repliées (continuation par espace ou tabulation) sont dépliées et
     * les noms de champs sont insensibles à la casse.
     * @param report Texte brut des champs.
     * @return Champs lus (valid = false si rien d'exploitable).
     */
    inline Mdn ParseDispositionNotification(std::wstring_view report)
    {
        /* dépliage des lignes */
        std::wstring unfolded;
        size_t pos = 0;
        while (pos <= report.size())
        {
            size_t nl = report.find(L'\n', pos);
            std::wstring_view line = report.substr(pos, nl == std::wstring_view::npos ? std::wstring_view::npos : nl - pos);
            if (!line.empty() && line.back() == L'\r')
                line.remove_suffix(1);
            if (!line.empty() && (line.front() == L' ' || line.front() == L'\t') && !unfolded.empty() && unfolded.back() != L'\n')
            {
                unfolded.push_back(L' ');
                unfolded.append(detail::Trim(line));
            }
            else
            {
                if (!unfolded.empty())
                    unfolded.push_back(L'\n');
                unfolded.append(line);
            }
            if (nl == std::wstring_view::npos)
                break;
            pos = nl + 1;
        }

        Mdn mdn;
        pos = 0;
        while (pos < unfolded.size())
        {
            size_t nl = unfolded.find(L'\n', pos);
            std::wstring_view line{ unfolded.data() + pos, (nl == std::wstring::npos ? unfolded.size() : nl) - pos };
            pos = nl == std::wstring::npos ? unfolded.size() : nl + 1;

            auto colon = line.find(L':');
            if (colon == std::wstring_view::npos)
                continue;
            auto name = detail::Lower(detail::Trim(line.substr(0, colon)));
            auto value = detail::Trim(line.substr(colon + 1));

            if (name == L"final-recipient")
            {
                auto semi = value.find(L';');
                mdn.final_recipient = detail::Trim(semi == std::wstring::npos ? value : value.substr(semi + 1));
            }
            else if (name == L"original-message-id")
            {
                mdn.original_message_id = value;
            }
            else if (name == L"disposition")
            {
                /* mode d'action/mode d'envoi; type[/modificateurs] */
                auto semi = value.find(L';');
                std::wstring type = detail::Trim(semi == std::wstring::npos ? value : value.substr(semi + 1));
                auto slash = type.find(L'/');
                if (slash != std::wstring::npos)
                    type = detail::Trim(type.substr(0, slash));
                mdn.disposition_type = detail::Lower(type);
            }
        }

        mdn.valid = !mdn.final_recipient.empty() || !mdn.disposition_type.empty();
        return mdn;
    }

    /**
     * @brief Texte de bandeau décrivant un accusé reçu, par exemple « Accusé de lecture : lu par X le … ».
     * @param mdn Champs analysés.
     * @param date_text Date de l'accusé, déjà formatée (peut être vide).
     * @param english true pour l'anglais, false pour le français.
     * @return Phrase localisée.
     */
    inline std::wstring DescribeMdn(Mdn const& mdn, std::wstring_view date_text, bool english)
    {
        std::wstring who = mdn.final_recipient.empty() ? (english ? L"the recipient" : L"le destinataire") : mdn.final_recipient;
        std::wstring when = date_text.empty() ? std::wstring{} : (english ? L" on " : L" le ") + std::wstring{ date_text };
        auto const& t = mdn.disposition_type;

        if (t == L"deleted")
            return (english ? L"Read receipt: deleted by " : L"Accusé de lecture : supprimé sans être lu par ") + who + when;
        if (t == L"denied")
            return (english ? L"Read receipt: declined by " : L"Accusé de lecture : refusé par ") + who + when;
        if (t == L"dispatched" || t == L"processed")
            return (english ? L"Delivery notification: processed by " : L"Notification de distribution : traité par ") + who + when;
        if (t == L"failed")
            return (english ? L"Delivery notification: processing failed for " : L"Notification de distribution : échec du traitement pour ") + who + when;
        return (english ? L"Read receipt: read by " : L"Accusé de lecture : lu par ") + who + when;
    }
}
