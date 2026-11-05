#pragma once

/* Suivi des messages : fonctions pures (sans dépendance à WinRT) partagées par
 * l'application et par les tests unitaires. */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::FollowUp
{
    /** @brief Choix d'échéance proposés dans les menus de suivi. */
    enum class Due
    {
        Today,     ///< aujourd'hui
        Tomorrow,  ///< demain
        ThisWeek,  ///< vendredi de la semaine en cours
        NextWeek,  ///< lundi de la semaine prochaine
        None       ///< sans date
    };

    /**
     * @brief Nombre de jours à ajouter à aujourd'hui pour atteindre l'échéance.
     * @param choice Choix d'échéance.
     * @param weekday Jour de la semaine d'aujourd'hui (0 = dimanche … 6 = samedi, comme tm_wday).
     * @return Décalage en jours, ou -1 pour « sans date ».
     */
    inline int DueOffsetDays(Due choice, int weekday)
    {
        switch (choice)
        {
        case Due::Today:    return 0;
        case Due::Tomorrow: return 1;
        case Due::ThisWeek: return (5 - weekday + 7) % 7;
        case Due::NextWeek:
        {
            int d = (8 - weekday) % 7;
            return d == 0 ? 7 : d;
        }
        default:            return -1;
        }
    }

    /**
     * @brief Libellés prédéfinis du suivi, dans l'ordre d'affichage des menus.
     * @return Liste des libellés.
     */
    inline std::vector<std::wstring> DefaultLabels()
    {
        return { L"Urgent", L"À traiter", L"À répondre", L"Pour information", L"En attente" };
    }

    /**
     * @brief Indique si l'échéance d'un suivi est dépassée.
     * @param due_epoch Échéance en secondes Unix (0 = aucune).
     * @param completed_epoch Date de fin en secondes Unix (0 = non terminé).
     * @param now_epoch Heure courante en secondes Unix.
     * @return true si le suivi n'est pas terminé et son échéance est passée.
     */
    inline bool IsOverdue(int64_t due_epoch, int64_t completed_epoch, int64_t now_epoch)
    {
        return completed_epoch == 0 && due_epoch > 0 && due_epoch < now_epoch;
    }

    /**
     * @brief Convertit un texte de secondes Unix (étiquette Camel) en entier.
     * @param text Texte (éventuellement vide ou invalide).
     * @return Valeur lue, 0 si le texte est vide ou n'est pas un nombre.
     */
    inline int64_t ParseEpoch(std::wstring_view text)
    {
        if (text.empty())
            return 0;
        int64_t value = 0;
        for (wchar_t c : text)
        {
            if (c < L'0' || c > L'9')
                return 0;
            value = value * 10 + (c - L'0');
        }
        return value;
    }
}
