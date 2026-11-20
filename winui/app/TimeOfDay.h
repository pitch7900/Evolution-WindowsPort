#pragma once

/* Heures de la journée saisies à la main (sélecteur d'heure des événements) : fonctions pures
 * (sans dépendance à WinRT) partagées par l'application et par les tests unitaires.
 * Une heure est exprimée en minutes depuis minuit (0 à 1439) ; -1 signale une saisie invalide. */

#include <cwchar>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::TimeOfDay
{
    /** Pas de la liste de sélection rapide, en minutes (un quart d'heure). */
    constexpr int kStepMinutes = 15;

    /** Nombre de minutes d'une journée. */
    constexpr int kMinutesPerDay = 24 * 60;

    /**
     * @brief Interprète une heure saisie librement.
     *
     * Formes acceptées : « 14:30 », « 14h30 », « 14.30 », « 14h », « 14 », « 1430 », « 930 »,
     * ainsi que « 2:30 pm » / « 2pm » / « 12am » (12 heures avec am/pm). Les espaces autour sont ignorés.
     * @param text saisie de l'utilisateur
     * @return minutes depuis minuit (0 à 1439), ou -1 si la saisie n'est pas une heure valide
     */
    inline int Parse(std::wstring_view text)
    {
        auto is_space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == 0x00A0; };
        while (!text.empty() && is_space(text.front()))
            text.remove_prefix(1);
        while (!text.empty() && is_space(text.back()))
            text.remove_suffix(1);

        /* suffixe am / pm */
        int meridiem = 0;   /* 0 : 24 heures, 1 : am, 2 : pm */
        if (text.size() >= 2)
        {
            wchar_t a = static_cast<wchar_t>(std::towlower(text[text.size() - 2]));
            wchar_t b = static_cast<wchar_t>(std::towlower(text.back()));
            if (b == L'm' && (a == L'a' || a == L'p'))
            {
                meridiem = (a == L'a') ? 1 : 2;
                text.remove_suffix(2);
                while (!text.empty() && is_space(text.back()))
                    text.remove_suffix(1);
            }
        }
        if (text.empty())
            return -1;

        /* groupes de chiffres séparés par un seul « : », « h » ou « . » */
        std::vector<std::wstring> groups(1);
        for (wchar_t c : text)
        {
            if (c >= L'0' && c <= L'9')
                groups.back() += c;
            else if ((c == L':' || c == L'h' || c == L'H' || c == L'.') && groups.size() == 1 && !groups.back().empty())
                groups.emplace_back();
            else
                return -1;
        }
        if (groups[0].empty() || groups[0].size() > 4)
            return -1;

        int hours = 0, minutes = 0;
        if (groups.size() == 1)
        {
            int value = std::stoi(groups[0]);
            if (groups[0].size() <= 2)
                hours = value;
            else
            {
                hours = value / 100;
                minutes = value % 100;
            }
        }
        else
        {
            if (groups[0].size() > 2 || groups[1].size() > 2)
                return -1;
            hours = std::stoi(groups[0]);
            minutes = groups[1].empty() ? 0 : std::stoi(groups[1]);
        }

        if (minutes > 59)
            return -1;
        if (meridiem != 0)
        {
            if (hours < 1 || hours > 12)
                return -1;
            hours = hours % 12 + (meridiem == 2 ? 12 : 0);
        }
        if (hours > 23)
            return -1;
        return hours * 60 + minutes;
    }

    /**
     * @brief Formate une heure en « HH:MM » (24 heures).
     * @param minutes minutes depuis minuit (ramenées dans la journée)
     * @return texte « HH:MM »
     */
    inline std::wstring Format(int minutes)
    {
        minutes = ((minutes % kMinutesPerDay) + kMinutesPerDay) % kMinutesPerDay;
        wchar_t buffer[8];
        swprintf(buffer, 8, L"%02d:%02d", minutes / 60, minutes % 60);
        return buffer;
    }

    /**
     * @brief Liste de la sélection rapide : de 00:00 à 23:45 par quarts d'heure.
     * @return textes « HH:MM » dans l'ordre
     */
    inline std::vector<std::wstring> QuickList()
    {
        std::vector<std::wstring> list;
        for (int minutes = 0; minutes < kMinutesPerDay; minutes += kStepMinutes)
            list.push_back(Format(minutes));
        return list;
    }
}
