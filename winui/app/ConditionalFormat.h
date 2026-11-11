#pragma once

/**
 * @file ConditionalFormat.h
 * @brief Mise en forme conditionnelle : couleur automatique d'un message ou d'un événement
 *        selon des règles (expéditeur, domaine, objet, importance, organisateur).
 *
 * Logique pure (sans WinRT) pour pouvoir être testée ; la persistance dans les
 * réglages et l'interface sont dans MainWindow.Categories.cpp.
 */

#include "Categories.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::ConditionalFormat
{
    /** @brief Critère testé par une règle. */
    enum class RuleField
    {
        Sender = 0,      ///< Expéditeur (nom ou adresse) contient le texte (courriels)
        SenderDomain,    ///< Domaine de l'adresse de l'expéditeur (courriels)
        Subject,         ///< Objet du message ou titre de l'événement contient le texte
        Importance,      ///< Importance du message : « haute » ou « basse » (courriels)
        Organizer        ///< Organisateur contient le texte (événements)
    };

    /** @brief Nombre de critères disponibles. */
    inline constexpr int RuleFieldCount = 5;

    /** @brief Une règle : si le critère correspond, l'élément prend la couleur. */
    struct FormatRule
    {
        bool enabled{ true };                  ///< Règle active
        RuleField field{ RuleField::Sender };  ///< Critère
        std::wstring text;                     ///< Texte recherché (ou « high » / « low » pour l'importance)
        uint8_t r{ 0 };                        ///< Couleur : rouge
        uint8_t g{ 120 };                      ///< Couleur : vert
        uint8_t b{ 212 };                      ///< Couleur : bleu
    };

    /** @brief Données d'un élément soumises aux règles (champs inconnus laissés vides). */
    struct RuleContext
    {
        std::wstring sender;      ///< Champ « De » complet (« Nom <adresse> »)
        std::wstring subject;     ///< Objet ou titre
        std::wstring importance;  ///< « high », « low » ou « normal »
        std::wstring organizer;   ///< Nom et adresse de l'organisateur
    };

    /**
     * @brief Domaine d'une adresse présente dans un champ « De » (partie après le dernier « @ »).
     * @param sender Texte du type « Nom <jean@exemple.fr> » ou « jean@exemple.fr ».
     * @return Domaine en minuscules sans chevron, vide si aucune adresse.
     */
    inline std::wstring DomainOf(std::wstring_view sender)
    {
        size_t at = sender.rfind(L'@');
        if (at == std::wstring_view::npos)
            return L"";
        size_t end = at + 1;
        while (end < sender.size() && sender[end] != L'>' && sender[end] != L' ' && sender[end] != L'"' && sender[end] != L',' && sender[end] != L';')
            ++end;
        return Categories::FoldName(sender.substr(at + 1, end - at - 1));
    }

    /**
     * @brief Indique si une règle correspond à un élément.
     * @param rule Règle testée (une règle désactivée ou sans texte ne correspond jamais).
     * @param context Données de l'élément.
     * @return true si le critère est satisfait.
     */
    inline bool RuleMatches(FormatRule const& rule, RuleContext const& context)
    {
        if (!rule.enabled)
            return false;
        std::wstring needle = Categories::FoldName(Categories::TrimName(rule.text));
        if (needle.empty())
            return false;

        auto contains = [&](std::wstring const& haystack)
        {
            return Categories::FoldName(haystack).find(needle) != std::wstring::npos;
        };

        switch (rule.field)
        {
        case RuleField::Sender:
            return contains(context.sender);
        case RuleField::SenderDomain:
        {
            while (!needle.empty() && needle.front() == L'@')
                needle.erase(needle.begin());
            std::wstring domain = DomainOf(context.sender);
            if (needle.empty() || domain.empty())
                return false;
            /* « exemple.fr » correspond à exemple.fr et à ses sous-domaines (mail.exemple.fr) */
            return domain == needle
                || (domain.size() > needle.size() && domain.compare(domain.size() - needle.size(), needle.size(), needle) == 0
                    && domain[domain.size() - needle.size() - 1] == L'.');
        }
        case RuleField::Subject:
            return contains(context.subject);
        case RuleField::Importance:
            return Categories::FoldName(context.importance) == needle;
        case RuleField::Organizer:
            return contains(context.organizer);
        }
        return false;
    }

    /**
     * @brief Couleur de la première règle active qui correspond.
     * @param rules Règles, par ordre de priorité.
     * @param context Données de l'élément.
     * @param r Rouge de sortie.
     * @param g Vert de sortie.
     * @param b Bleu de sortie.
     * @return true si une règle correspond (les couleurs sont alors renseignées).
     */
    inline bool FindRuleColor(std::vector<FormatRule> const& rules, RuleContext const& context, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        for (auto const& rule : rules)
        {
            if (RuleMatches(rule, context))
            {
                r = rule.r;
                g = rule.g;
                b = rule.b;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Sérialise les règles : une règle par ligne, champs séparés par des tabulations
     *        (actif, critère, couleur « #RRGGBB », texte sans tabulation ni saut de ligne).
     * @param rules Règles à écrire.
     * @return Texte à stocker dans les réglages.
     */
    inline std::wstring Serialize(std::vector<FormatRule> const& rules)
    {
        std::wstring out;
        for (auto const& rule : rules)
        {
            std::wstring text = rule.text;
            for (auto& ch : text)
                if (ch == L'\t' || ch == L'\n' || ch == L'\r')
                    ch = L' ';
            out += rule.enabled ? L"1" : L"0";
            out += L'\t';
            out += std::to_wstring(static_cast<int>(rule.field));
            out += L'\t';
            out += Categories::FormatHexColor(rule.r, rule.g, rule.b);
            out += L'\t';
            out += text;
            out += L'\n';
        }
        return out;
    }

    /**
     * @brief Relit des règles écrites par Serialize() ; les lignes illisibles sont ignorées.
     * @param text Texte des réglages.
     * @return Règles lues, dans l'ordre.
     */
    inline std::vector<FormatRule> Deserialize(std::wstring_view text)
    {
        std::vector<FormatRule> rules;
        size_t start = 0;
        while (start < text.size())
        {
            size_t stop = text.find(L'\n', start);
            std::wstring_view line = text.substr(start, stop == std::wstring_view::npos ? std::wstring_view::npos : stop - start);
            start = stop == std::wstring_view::npos ? text.size() : stop + 1;

            std::wstring_view parts[4];
            size_t pos = 0;
            bool ok = true;
            for (int i = 0; i < 4 && ok; ++i)
            {
                size_t tab = line.find(L'\t', pos);
                if (i < 3)
                {
                    if (tab == std::wstring_view::npos)
                        ok = false;
                    else
                    {
                        parts[i] = line.substr(pos, tab - pos);
                        pos = tab + 1;
                    }
                }
                else
                {
                    parts[i] = line.substr(pos);
                }
            }
            if (!ok || parts[1].size() != 1 || parts[1][0] < L'0' || parts[1][0] >= L'0' + RuleFieldCount)
                continue;

            FormatRule rule;
            rule.enabled = parts[0] == L"1";
            rule.field = static_cast<RuleField>(parts[1][0] - L'0');
            if (!Categories::ParseHexColor(parts[2], rule.r, rule.g, rule.b))
                continue;
            rule.text = std::wstring{ parts[3] };
            rules.push_back(std::move(rule));
        }
        return rules;
    }
}
