#pragma once

/* Contrôle d'intégrité des mises à jour : fonctions pures (sans dépendance à
 * WinRT) partagées par l'application et par les tests unitaires. */

#include <cwctype>
#include <string>
#include <string_view>

namespace EvolutionWin::UpdateIntegrity
{
    /**
     * @brief Indique si le texte est une empreinte SHA-256 hexadécimale (64 chiffres).
     * @param text Texte à examiner.
     * @return true si le texte comporte exactement 64 chiffres hexadécimaux.
     */
    inline bool IsHexSha256(std::wstring_view text)
    {
        if (text.size() != 64)
            return false;
        for (wchar_t c : text)
            if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F')))
                return false;
        return true;
    }

    /**
     * @brief Compare deux textes sans tenir compte de la casse ASCII.
     * @param a Premier texte.
     * @param b Second texte.
     * @return true si les textes sont égaux à la casse près.
     */
    inline bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (std::towlower(a[i]) != std::towlower(b[i]))
                return false;
        return true;
    }

    /**
     * @brief Extrait l'empreinte d'un fichier au format « sha256sum » (« <hex> *<nom> » par ligne).
     * @param text Contenu du fichier .sha256 publié avec la version.
     * @param file_name Nom du fichier dont on veut l'empreinte (installeur MSI).
     * @return Empreinte en minuscules, ou chaîne vide si aucune ligne valide ne correspond.
     *         Une ligne sans nom (empreinte seule) est acceptée lorsqu'elle est la seule.
     */
    inline std::wstring ParseSha256File(std::wstring_view text, std::wstring_view file_name)
    {
        std::wstring fallback;
        int valid_lines = 0;
        size_t pos = 0;

        while (pos <= text.size())
        {
            size_t end = text.find_first_of(L"\r\n", pos);
            if (end == std::wstring_view::npos)
                end = text.size();
            std::wstring_view line = text.substr(pos, end - pos);
            pos = end + 1;

            while (!line.empty() && std::iswspace(line.front()))
                line.remove_prefix(1);
            while (!line.empty() && std::iswspace(line.back()))
                line.remove_suffix(1);
            if (line.size() < 64 || !IsHexSha256(line.substr(0, 64)))
                continue;

            std::wstring hash;
            for (wchar_t c : line.substr(0, 64))
                hash += static_cast<wchar_t>(std::towlower(c));

            std::wstring_view rest = line.substr(64);
            while (!rest.empty() && (std::iswspace(rest.front()) || rest.front() == L'*'))
                rest.remove_prefix(1);

            ++valid_lines;
            if (rest.empty() || valid_lines == 1)
                if (fallback.empty())
                    fallback = hash;
            if (EqualsNoCase(rest, file_name))
                return hash;
        }

        return valid_lines == 1 ? fallback : std::wstring{};
    }

    /**
     * @brief Convertit des octets en texte hexadécimal minuscule.
     * @param data Octets à convertir.
     * @param size Nombre d'octets.
     * @return Texte hexadécimal (2 caractères par octet).
     */
    inline std::wstring ToHex(const unsigned char* data, size_t size)
    {
        static constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring text;
        text.reserve(size * 2);
        for (size_t i = 0; i < size; ++i)
        {
            text += digits[data[i] >> 4];
            text += digits[data[i] & 0x0F];
        }
        return text;
    }
}
