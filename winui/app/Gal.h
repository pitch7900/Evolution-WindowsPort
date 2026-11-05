#pragma once

/* Annuaire de l'organisation (carnet d'adresses global, GAL) : fonctions pures
 * (sans dépendance à WinRT) partagées par l'application et par les tests
 * unitaires : formatage de la fiche, extraction et comparaison d'adresses pour
 * fusionner les suggestions de destinataires, règles de déclenchement. */

#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::Gal
{
    /** Nombre minimal de caractères pour interroger l'annuaire depuis la vue Contacts. */
    constexpr size_t kMinQueryContacts = 2;

    /** Nombre minimal de caractères pour interroger l'annuaire pendant la saisie d'un destinataire. */
    constexpr size_t kMinQueryCompose = 3;

    /** @brief Fiche d'annuaire (champs de l'objet JSON renvoyé par evo_core_search_gal). */
    struct Person
    {
        std::wstring name;
        std::wstring email;
        std::wstring title;
        std::wstring department;
        std::wstring company;
        std::wstring office;
        std::wstring phone;
        std::wstring mobile;
        std::wstring account;
    };

    /**
     * @brief Retire les espaces (et tabulations) de début et de fin d'un texte.
     * @param text Texte à rogner.
     * @return Texte rogné.
     */
    inline std::wstring Trim(std::wstring_view text)
    {
        size_t begin = 0, end = text.size();
        while (begin < end && (text[begin] == L' ' || text[begin] == L'\t'))
            ++begin;
        while (end > begin && (text[end - 1] == L' ' || text[end - 1] == L'\t'))
            --end;
        return std::wstring{ text.substr(begin, end - begin) };
    }

    /**
     * @brief Indique si une saisie est assez longue pour interroger l'annuaire.
     * @param text Saisie (les espaces de début et de fin sont ignorés).
     * @param minimum Nombre minimal de caractères (kMinQueryContacts ou kMinQueryCompose).
     * @return true si la recherche est permise.
     */
    inline bool QueryEligible(std::wstring_view text, size_t minimum)
    {
        return Trim(text).size() >= minimum;
    }

    /**
     * @brief Nom à afficher pour une fiche : nom complet, à défaut l'adresse.
     * @param person Fiche d'annuaire.
     * @return Nom affiché.
     */
    inline std::wstring DisplayName(Person const& person)
    {
        return person.name.empty() ? person.email : person.name;
    }

    /**
     * @brief Concatène les éléments non vides avec un séparateur.
     * @param parts Éléments à joindre.
     * @param separator Séparateur inséré entre deux éléments non vides.
     * @return Texte joint (vide si tous les éléments sont vides).
     */
    inline std::wstring JoinNonEmpty(std::vector<std::wstring> const& parts, std::wstring_view separator)
    {
        std::wstring out;
        for (auto const& part : parts)
        {
            std::wstring value = Trim(part);
            if (value.empty())
                continue;
            if (!out.empty())
                out += separator;
            out += value;
        }
        return out;
    }

    /**
     * @brief Ligne « fonction · service · société » d'une fiche.
     * @param person Fiche d'annuaire.
     * @return Texte joint par « · » (vide si aucune information).
     */
    inline std::wstring OrganizationLine(Person const& person)
    {
        return JoinNonEmpty({ person.title, person.department, person.company }, L" · ");
    }

    /**
     * @brief Ligne des numéros de téléphone d'une fiche (fixe, puis mobile).
     * @param person Fiche d'annuaire.
     * @param mobile_label Libellé localisé placé devant le mobile (« mobile »).
     * @return Numéros séparés par « ; » (vide si aucun).
     */
    inline std::wstring PhoneLine(Person const& person, std::wstring_view mobile_label)
    {
        std::wstring mobile = Trim(person.mobile);
        if (!mobile.empty())
            mobile = std::wstring{ mobile_label } + L" " + mobile;
        return JoinNonEmpty({ person.phone, mobile }, L" ; ");
    }

    /**
     * @brief Note de la fiche : bureau et compte d'annuaire d'origine.
     * @param person Fiche d'annuaire.
     * @param office_label Libellé localisé placé devant le bureau (« Bureau »).
     * @param directory_label Libellé localisé placé devant le compte (« Annuaire »).
     * @return Texte sur une ligne (vide si ni bureau ni compte).
     */
    inline std::wstring NoteLine(Person const& person, std::wstring_view office_label, std::wstring_view directory_label)
    {
        std::wstring office = Trim(person.office);
        std::wstring account = Trim(person.account);
        if (!office.empty())
            office = std::wstring{ office_label } + L" : " + office;
        if (!account.empty())
            account = std::wstring{ directory_label } + L" : " + account;
        return JoinNonEmpty({ office, account }, L" · ");
    }

    /**
     * @brief Adresse prête à être insérée dans À / Cc / Cci : « Nom <adresse> » ou l'adresse seule.
     * @param person Fiche d'annuaire.
     * @return Adresse de destinataire.
     */
    inline std::wstring RecipientAddress(Person const& person)
    {
        std::wstring name = Trim(person.name);
        return name.empty() ? person.email : name + L" <" + person.email + L">";
    }

    /**
     * @brief Met une adresse en minuscules (ASCII) pour la comparer.
     * @param text Adresse électronique.
     * @return Adresse en minuscules.
     */
    inline std::wstring LowerAscii(std::wstring text)
    {
        for (auto& c : text)
            if (c >= L'A' && c <= L'Z')
                c = static_cast<wchar_t>(c - L'A' + L'a');
        return text;
    }

    /**
     * @brief Extrait l'adresse électronique d'un destinataire « Nom <adresse> » (ou d'une adresse seule).
     * @param recipient Texte du destinataire, éventuellement suivi d'un libellé « (récent) ».
     * @return Adresse en minuscules, sans chevrons (vide si le texte est vide).
     */
    inline std::wstring ExtractEmail(std::wstring_view recipient)
    {
        size_t open = recipient.rfind(L'<');
        size_t close = open == std::wstring_view::npos ? std::wstring_view::npos : recipient.find(L'>', open);
        if (open != std::wstring_view::npos && close != std::wstring_view::npos)
            return LowerAscii(Trim(recipient.substr(open + 1, close - open - 1)));
        return LowerAscii(Trim(recipient));
    }

    /**
     * @brief Indique si une adresse figure déjà parmi des destinataires (comparaison insensible à la casse).
     * @param existing Destinataires déjà proposés (« Nom <adresse> » ou adresses seules).
     * @param email Adresse recherchée.
     * @return true si l'adresse est déjà présente.
     */
    inline bool ContainsEmail(std::vector<std::wstring> const& existing, std::wstring_view email)
    {
        std::wstring wanted = LowerAscii(Trim(email));
        for (auto const& entry : existing)
            if (ExtractEmail(entry) == wanted)
                return true;
        return false;
    }
}
