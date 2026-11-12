#pragma once

/**
 * @file DataTransfer.h
 * @brief Fonctions autonomes (sans WinRT ni interface) des assistants d'exportation et
 * d'importation de données : détection des dossiers de courrier d'un profil Thunderbird,
 * reconnaissance des fichiers Outlook (.pst / .ost), conversion de plage de dates.
 *
 * Utilisé par MainWindow.Transfer.cpp et testé par tests/unit/test_datatransfer.cpp.
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace EvolutionWin::DataTransfer
{
    /** Dossier de courrier proposé à l'importation. */
    struct SourceDir
    {
        std::wstring label; /**< libellé affiché (profil — compte) */
        std::wstring path;  /**< chemin du dossier de courrier du compte */
    };

    /**
     * @brief Texte en minuscules (ASCII) pour comparer des extensions.
     * @param text texte d'origine
     * @return texte en minuscules
     */
    inline std::wstring LowerAscii(std::wstring text)
    {
        for (auto& c : text)
            if (c >= L'A' && c <= L'Z')
                c = static_cast<wchar_t>(c + 32);
        return text;
    }

    /**
     * @brief Indique si un chemin désigne un fichier de données Outlook (.pst ou .ost),
     * que le moteur ne sait pas lire.
     * @param path chemin ou nom de fichier
     * @return true pour .pst et .ost (casse ignorée)
     */
    inline bool IsOutlookDataFile(std::wstring const& path)
    {
        auto ext = LowerAscii(std::filesystem::path{ path }.extension().wstring());
        return ext == L".pst" || ext == L".ost";
    }

    /**
     * @brief Nom d'un profil Thunderbird sans le préfixe aléatoire (« ab12cd34.default-release »
     * devient « default-release »).
     * @param folder_name nom du dossier du profil
     * @return nom lisible du profil
     */
    inline std::wstring ProfileDisplayName(std::wstring const& folder_name)
    {
        auto dot = folder_name.find(L'.');
        return dot != std::wstring::npos && dot + 1 < folder_name.size() ? folder_name.substr(dot + 1) : folder_name;
    }

    /**
     * @brief Liste les dossiers de courrier des profils Thunderbird : pour chaque profil,
     * un dossier par compte sous « Mail » (dossiers locaux, POP) et « ImapMail » (IMAP).
     * Un compte sans aucun fichier n'est pas proposé.
     * @param profiles_root dossier des profils (%APPDATA%\Thunderbird\Profiles)
     * @return dossiers trouvés, triés par libellé (liste vide si Thunderbird est absent)
     */
    inline std::vector<SourceDir> FindThunderbirdMailDirs(std::filesystem::path const& profiles_root)
    {
        namespace fs = std::filesystem;
        std::vector<SourceDir> found;
        std::error_code ec;

        for (fs::directory_iterator profile(profiles_root, ec), end; !ec && profile != end; profile.increment(ec))
        {
            if (!profile->is_directory(ec))
                continue;
            for (auto const* sub : { L"Mail", L"ImapMail" })
            {
                fs::path mail_root = profile->path() / sub;
                for (fs::directory_iterator account(mail_root, ec), account_end; !ec && account != account_end;
                     account.increment(ec))
                {
                    if (!account->is_directory(ec) || fs::is_empty(account->path(), ec))
                        continue;
                    found.push_back({ ProfileDisplayName(profile->path().filename().wstring()) + L" — "
                                          + account->path().filename().wstring(),
                        account->path().wstring() });
                }
                ec.clear();
            }
            ec.clear();
        }
        std::sort(found.begin(), found.end(), [](SourceDir const& a, SourceDir const& b) { return a.label < b.label; });
        return found;
    }

    /**
     * @brief Borne de fin de période : la date choisie incluse, jusqu'à 23 h 59 min 59 s.
     * @param day_start début du jour choisi (secondes depuis 1970)
     * @return dernière seconde du jour
     */
    inline int64_t EndOfDay(int64_t day_start)
    {
        return day_start + 86399;
    }

    /**
     * @brief Chemin d'un dossier de destination d'importation : nom saisi par l'utilisateur,
     * nettoyé des espaces et des séparateurs en début et fin, « \ » remplacé par « / ».
     * @param typed texte saisi
     * @return chemin de dossier (« A/B »), vide si rien d'exploitable
     */
    inline std::wstring NormalizeFolderPath(std::wstring typed)
    {
        std::wstring out, segment;
        auto flush = [&]()
        {
            size_t first = segment.find_first_not_of(L" \t");
            size_t last = segment.find_last_not_of(L" \t");
            if (first != std::wstring::npos)
            {
                if (!out.empty())
                    out.push_back(L'/');
                out.append(segment, first, last - first + 1);
            }
            segment.clear();
        };
        for (auto c : typed)
        {
            if (c == L'/' || c == L'\\')
                flush();
            else
                segment.push_back(c);
        }
        flush();
        return out;
    }
}
