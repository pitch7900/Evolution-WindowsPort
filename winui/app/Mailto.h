#pragma once
/**
 * @file Mailto.h
 * @brief Logique pure de la prise en charge des liens mailto: : analyse d'une URL (RFC 6068),
 *        recherche de l'URL dans une ligne de commande et description des clés de registre qui font
 *        d'EvolutionWin une application de messagerie Windows. Aucune dépendance à WinRT ni au
 *        registre : testé unitairement (tests/unit/test_mailto.cpp).
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::Mailto
{
    /** Message préparé par un lien mailto: (champs déjà décodés). */
    struct Message
    {
        std::wstring to;
        std::wstring cc;
        std::wstring bcc;
        std::wstring subject;
        std::wstring body;
    };

    namespace detail
    {
        /** @brief Valeur hexadécimale d'un chiffre, ou -1. */
        inline int HexValue(wchar_t c)
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return 10 + (c - L'a');
            if (c >= L'A' && c <= L'F') return 10 + (c - L'A');
            return -1;
        }

        /** @brief Minuscules ASCII d'un caractère. */
        inline wchar_t Lower(wchar_t c)
        {
            return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        }

        /** @brief Compare sans casse (ASCII) le début d'un texte avec un préfixe. */
        inline bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix)
        {
            if (text.size() < prefix.size())
                return false;
            for (size_t i = 0; i < prefix.size(); ++i)
                if (Lower(text[i]) != Lower(prefix[i]))
                    return false;
            return true;
        }

        /** @brief Ajoute un point de code Unicode à une chaîne UTF-16. */
        inline void AppendCodePoint(std::wstring& out, uint32_t cp)
        {
            if (cp >= 0x10000 && cp <= 0x10FFFF)
            {
                cp -= 0x10000;
                out += static_cast<wchar_t>(0xD800 + (cp >> 10));
                out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            }
            else
                out += static_cast<wchar_t>(cp);
        }

        /**
         * @brief Décode une suite d'octets UTF-8 en UTF-16 ; chaque octet invalide devient U+FFFD.
         */
        inline std::wstring Utf8ToWide(std::string const& bytes)
        {
            std::wstring out;
            size_t i = 0;
            while (i < bytes.size())
            {
                unsigned char b = static_cast<unsigned char>(bytes[i]);
                if (b < 0x80) { out += static_cast<wchar_t>(b); ++i; continue; }

                int extra = (b >= 0xF0 && b <= 0xF4) ? 3 : (b >= 0xE0 && b < 0xF0) ? 2 : (b >= 0xC2 && b < 0xE0) ? 1 : -1;
                if (extra < 0 || i + static_cast<size_t>(extra) >= bytes.size())
                {
                    out += L'�';
                    ++i;
                    continue;
                }
                uint32_t cp = b & (0x3F >> extra);
                bool valid = true;
                for (int k = 1; k <= extra; ++k)
                {
                    unsigned char c = static_cast<unsigned char>(bytes[i + k]);
                    if ((c & 0xC0) != 0x80) { valid = false; break; }
                    cp = (cp << 6) | (c & 0x3F);
                }
                if (!valid || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                {
                    out += L'�';
                    ++i;
                    continue;
                }
                AppendCodePoint(out, cp);
                i += static_cast<size_t>(extra) + 1;
            }
            return out;
        }

        /**
         * @brief Décode un composant d'URL : %XX (octets UTF-8) ; le + reste un +, conformément à la RFC 6068.
         * @param text Composant encodé.
         * @return Texte décodé (UTF-16).
         */
        inline std::wstring PercentDecode(std::wstring_view text)
        {
            std::wstring out;
            std::string pending;
            auto flush = [&]()
            {
                if (!pending.empty())
                {
                    out += Utf8ToWide(pending);
                    pending.clear();
                }
            };
            for (size_t i = 0; i < text.size(); ++i)
            {
                wchar_t c = text[i];
                if (c == L'%' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0)
                {
                    pending += static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2]));
                    i += 2;
                    continue;
                }
                flush();
                out += c;
            }
            flush();
            return out;
        }

        /** @brief Remplace les retours à la ligne par des espaces (champs d'en-tête : pas d'injection d'en-têtes). */
        inline std::wstring SingleLine(std::wstring text)
        {
            std::wstring flat;
            bool last_was_break = false;
            for (wchar_t c : text)
            {
                bool is_break = (c == L'\r' || c == L'\n' || c == L'\t');
                if (is_break && last_was_break)
                    continue;
                flat += is_break ? L' ' : c;
                last_was_break = is_break;
            }
            text = flat;
            size_t first = text.find_first_not_of(L' ');
            if (first == std::wstring::npos)
                return {};
            size_t last = text.find_last_not_of(L' ');
            return text.substr(first, last - first + 1);
        }

        /** @brief Ajoute des adresses à une liste séparée par « , » (les listes vides sont ignorées). */
        inline void AppendAddresses(std::wstring& list, std::wstring const& more)
        {
            std::wstring clean = SingleLine(more);
            if (clean.empty())
                return;
            if (!list.empty())
                list += L", ";
            list += clean;
        }

        /** @brief Convertit les fins de ligne CRLF et CR en LF (le corps de la rédaction utilise LF). */
        inline std::wstring NormalizeNewlines(std::wstring const& text)
        {
            std::wstring out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'\r')
                {
                    out += L'\n';
                    if (i + 1 < text.size() && text[i + 1] == L'\n')
                        ++i;
                }
                else
                    out += text[i];
            }
            return out;
        }
    }

    /**
     * @brief Indique si une chaîne est une URL mailto: (schéma insensible à la casse).
     * @param url Chaîne à tester.
     * @return true si elle commence par « mailto: ».
     */
    inline bool IsMailtoUrl(std::wstring_view url)
    {
        return detail::StartsWithNoCase(url, L"mailto:");
    }

    /**
     * @brief Analyse une URL mailto: (RFC 6068) : adresses de la partie chemin, puis champs to, cc, bcc,
     *        subject et body de la partie requête (les autres champs sont ignorés). Les adresses de
     *        plusieurs champs « to » sont cumulées ; les retours à la ligne sont retirés des champs
     *        d'en-tête ; le corps garde ses sauts de ligne (normalisés en LF).
     * @param url URL complète, par exemple mailto:a@b.fr?subject=Bonjour%20!&body=Texte
     * @param[out] message Message préparé (vide si l'analyse échoue).
     * @return true si l'URL est une URL mailto: valide.
     */
    inline bool Parse(std::wstring_view url, Message& message)
    {
        message = {};
        if (!IsMailtoUrl(url))
            return false;
        url.remove_prefix(7);
        /* mailto://a@b.fr : forme tolérée écrite par certains navigateurs */
        if (url.substr(0, 2) == L"//")
            url.remove_prefix(2);

        size_t fragment = url.find(L'#');
        if (fragment != std::wstring_view::npos)
            url = url.substr(0, fragment);

        size_t question = url.find(L'?');
        std::wstring_view path = url.substr(0, question);
        std::wstring_view query = question == std::wstring_view::npos ? std::wstring_view{} : url.substr(question + 1);

        while (!path.empty() && path.back() == L'/')
            path.remove_suffix(1);
        detail::AppendAddresses(message.to, detail::PercentDecode(path));

        size_t start = 0;
        while (start < query.size())
        {
            size_t end = query.find(L'&', start);
            std::wstring_view pair = query.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
            start = end == std::wstring_view::npos ? query.size() : end + 1;

            size_t equals = pair.find(L'=');
            if (equals == std::wstring_view::npos)
                continue;
            std::wstring key = detail::PercentDecode(pair.substr(0, equals));
            for (auto& c : key)
                c = detail::Lower(c);
            std::wstring value = detail::PercentDecode(pair.substr(equals + 1));

            if (key == L"to") detail::AppendAddresses(message.to, value);
            else if (key == L"cc") detail::AppendAddresses(message.cc, value);
            else if (key == L"bcc") detail::AppendAddresses(message.bcc, value);
            else if (key == L"subject") message.subject = detail::SingleLine(value);
            else if (key == L"body") message.body = detail::NormalizeNewlines(value);
        }
        return true;
    }

    /**
     * @brief Cherche une URL mailto: dans une ligne de commande Windows (guillemets acceptés).
     * @param command_line Ligne de commande complète (programme compris).
     * @return L'URL trouvée, ou une chaîne vide.
     */
    inline std::wstring FindUrlInCommandLine(std::wstring_view command_line)
    {
        size_t pos = 0;
        while (pos < command_line.size())
        {
            while (pos < command_line.size() && command_line[pos] == L' ')
                ++pos;
            if (pos >= command_line.size())
                break;

            std::wstring token;
            if (command_line[pos] == L'"')
            {
                size_t close = command_line.find(L'"', pos + 1);
                token = std::wstring{ command_line.substr(pos + 1, close == std::wstring_view::npos ? std::wstring_view::npos : close - pos - 1) };
                pos = close == std::wstring_view::npos ? command_line.size() : close + 1;
            }
            else
            {
                size_t space = command_line.find(L' ', pos);
                token = std::wstring{ command_line.substr(pos, space == std::wstring_view::npos ? std::wstring_view::npos : space - pos) };
                pos = space == std::wstring_view::npos ? command_line.size() : space;
            }
            if (IsMailtoUrl(token))
                return token;
        }
        return {};
    }

    /** Valeur de registre (chaîne) relative à HKEY_CURRENT_USER ; value_name vide = valeur par défaut. */
    struct RegistryValue
    {
        std::wstring key;
        std::wstring value_name;
        std::wstring data;
    };

    /** Identifiants de l'enregistrement (clés sous HKCU\Software). */
    inline constexpr wchar_t kAppKeyName[] = L"EvolutionWin";
    inline constexpr wchar_t kProtocolClass[] = L"EvolutionWin.Url.mailto";

    /** @brief Chemin (relatif à HKCU) de la clé du client de messagerie. */
    inline std::wstring ClientKey()
    {
        return std::wstring{ L"Software\\Clients\\Mail\\" } + kAppKeyName;
    }

    /** @brief Chemin (relatif à HKCU) de la classe du protocole mailto. */
    inline std::wstring ProtocolKey()
    {
        return std::wstring{ L"Software\\Classes\\" } + kProtocolClass;
    }

    /**
     * @brief Liste des valeurs à écrire pour déclarer EvolutionWin comme client de messagerie Windows
     *        et gestionnaire du protocole mailto: (capacités, associations d'URL, commande d'ouverture).
     * @param exe_path Chemin complet de EvolutionWin.exe.
     * @param app_name Nom affiché dans « Applications par défaut ».
     * @param description Description affichée.
     * @return Valeurs de registre (relatives à HKEY_CURRENT_USER).
     */
    inline std::vector<RegistryValue> BuildRegistration(std::wstring const& exe_path, std::wstring const& app_name,
        std::wstring const& description)
    {
        std::wstring quoted = L"\"" + exe_path + L"\"";
        std::wstring client = ClientKey();
        std::wstring protocol = ProtocolKey();
        return {
            { client, L"", app_name },
            { client + L"\\Capabilities", L"ApplicationName", app_name },
            { client + L"\\Capabilities", L"ApplicationDescription", description },
            { client + L"\\Capabilities\\URLAssociations", L"mailto", kProtocolClass },
            { client + L"\\DefaultIcon", L"", exe_path + L",0" },
            { client + L"\\shell\\open\\command", L"", quoted },
            { L"Software\\RegisteredApplications", kAppKeyName, client + L"\\Capabilities" },
            { protocol, L"", L"URL:MailTo Protocol" },
            { protocol, L"URL Protocol", L"" },
            { protocol + L"\\DefaultIcon", L"", exe_path + L",0" },
            { protocol + L"\\shell\\open\\command", L"", quoted + L" \"%1\"" },
        };
    }

    /**
     * @brief Clés entières à supprimer pour retirer l'enregistrement (la valeur de RegisteredApplications
     *        se supprime à part : voir RegisteredApplicationsKey).
     * @return Chemins relatifs à HKEY_CURRENT_USER.
     */
    inline std::vector<std::wstring> RegistrationKeysToDelete()
    {
        return { ClientKey(), ProtocolKey() };
    }

    /**
     * @brief Indique si la commande d'ouverture enregistrée correspond à l'exécutable courant
     *        (sans tenir compte de la casse : les chemins Windows ne la distinguent pas).
     * @param registered_command Valeur lue dans shell\open\command (avec ou sans guillemets).
     * @param exe_path Chemin complet de EvolutionWin.exe.
     * @return true si l'enregistrement pointe vers cet exécutable.
     */
    inline bool CommandMatchesExe(std::wstring_view registered_command, std::wstring_view exe_path)
    {
        if (!registered_command.empty() && registered_command.front() == L'"')
            registered_command.remove_prefix(1);
        if (registered_command.size() < exe_path.size())
            return false;
        std::wstring_view head = registered_command.substr(0, exe_path.size());
        for (size_t i = 0; i < exe_path.size(); ++i)
            if (detail::Lower(head[i]) != detail::Lower(exe_path[i]))
                return false;
        std::wstring_view rest = registered_command.substr(exe_path.size());
        return rest.empty() || rest.front() == L'"' || rest.front() == L' ';
    }
}
