#pragma once
/**
 * @file ToastNotifications.h
 * @brief Logique pure des notifications système (toasts) de courrier et du minuteur de relève : lecture des
 *        arguments d'un toast cliqué, classement de l'action demandée, composition des lignes d'un toast
 *        de nouveau message et conversion de l'intervalle de vérification du courrier. Aucune dépendance à
 *        WinRT : testé unitairement (tests/unit/test_toast.cpp).
 */

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace EvolutionWin::Toast
{
    /** Action demandée par un clic sur un toast ou l'un de ses boutons. */
    enum class Action
    {
        Open,       ///< clic sur le corps du toast d'un message : ouvrir le message
        Event,      ///< clic sur un rappel de calendrier : ouvrir le calendrier
        Delete,     ///< bouton « Supprimer » : message vers la Corbeille
        Flag,       ///< bouton « Marquer » : message suivi
        Dismiss     ///< bouton « Fermer » : aucune navigation
    };

    /** Arguments décodés d'un toast. */
    struct Arguments
    {
        Action action{ Action::Open };
        std::wstring account;
        std::wstring folder;
        std::wstring uid;

        /** @return true si le message est entièrement désigné (compte, dossier et identifiant). */
        bool HasMessage() const { return !account.empty() && !folder.empty() && !uid.empty(); }
    };

    /**
     * @brief Classe la valeur de l'argument « action » d'un toast.
     * @param value Valeur reçue (vide ou inconnue : clic sur le corps d'un message).
     * @return Action correspondante.
     */
    inline Action ClassifyAction(std::wstring_view value)
    {
        if (value == L"delete")
            return Action::Delete;
        if (value == L"flag")
            return Action::Flag;
        if (value == L"dismiss")
            return Action::Dismiss;
        if (value == L"event")
            return Action::Event;
        return Action::Open;
    }

    /**
     * @brief Décode les paires clé/valeur d'un toast cliqué.
     * @param pairs Arguments du toast (action, account, folder, uid) ; les clés inconnues sont ignorées.
     * @return Arguments décodés.
     */
    inline Arguments ParseArguments(std::vector<std::pair<std::wstring, std::wstring>> const& pairs)
    {
        Arguments result;
        for (auto const& [key, value] : pairs)
        {
            if (key == L"action")
                result.action = ClassifyAction(value);
            else if (key == L"account")
                result.account = value;
            else if (key == L"folder")
                result.folder = value;
            else if (key == L"uid")
                result.uid = value;
        }
        return result;
    }

    /** Lignes de texte d'un toast de nouveau message (la troisième peut être vide : alors omise). */
    struct Lines
    {
        std::wstring title;
        std::wstring first;
        std::wstring second;
    };

    /**
     * @brief Compose les lignes d'un toast de nouveau message : titre, expéditeur, objet.
     * @param title Titre déjà traduit (« Nouveau message » ou « N nouveaux messages »).
     * @param from Expéditeur (nom affichable).
     * @param subject Objet du message.
     * @param confidential Mode confidentiel : ni expéditeur ni objet.
     * @param confidential_text Texte neutre déjà traduit affiché en mode confidentiel.
     * @return Lignes du toast ; une ligne d'objet vide n'est pas affichée par l'appelant.
     */
    inline Lines ComposeNewMail(std::wstring_view title, std::wstring_view from, std::wstring_view subject,
                                bool confidential, std::wstring_view confidential_text)
    {
        Lines lines;
        lines.title = title;
        if (confidential)
        {
            lines.first = confidential_text;
            return lines;
        }
        lines.first = from;
        lines.second = subject;
        return lines;
    }

    /** Intervalle par défaut de la relève automatique du courrier (secondes). */
    inline constexpr unsigned kDefaultRefreshSeconds = 5 * 60;
    /** Intervalle maximal accepté (24 heures) : protège contre une valeur aberrante du fichier de réglages. */
    inline constexpr unsigned kMaxRefreshMinutes = 24 * 60;

    /**
     * @brief Convertit le réglage « mail.sync-interval » (minutes) en secondes pour le moteur.
     * @param minutes Valeur du réglage : « 0 » = manuellement uniquement ; vide, négative ou non numérique :
     *        valeur par défaut (5 minutes) ; plafonnée à 24 heures.
     * @return Intervalle en secondes (0 : relève automatique désactivée).
     */
    inline unsigned RefreshSeconds(std::wstring_view minutes)
    {
        if (minutes.empty())
            return kDefaultRefreshSeconds;
        unsigned long long value = 0;
        for (wchar_t c : minutes)
        {
            if (c < L'0' || c > L'9')
                return kDefaultRefreshSeconds;
            value = value * 10 + static_cast<unsigned>(c - L'0');
            if (value > kMaxRefreshMinutes)
                value = kMaxRefreshMinutes;
        }
        return static_cast<unsigned>(value) * 60u;
    }
}
