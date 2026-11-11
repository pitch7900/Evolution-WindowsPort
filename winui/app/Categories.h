#pragma once

/**
 * @file Categories.h
 * @brief Catégories unifiées (nom + couleur) partagées par les courriels, contacts,
 *        tâches et événements de calendrier, et logique de filtrage par catégorie.
 *
 * En-tête autonome (bibliothèque standard + Windows, sans WinRT) afin que le
 * registre, sa sérialisation et le filtrage soient testables unitairement
 * (tests/unit/test_categories.cpp).
 *
 * Correspondance avec les stockages natifs d'Evolution Data Server :
 * - courriels : étiquettes (user-flags) Camel portant le nom de la catégorie ;
 * - contacts : champ vCard CATEGORIES ;
 * - événements et tâches : propriétés iCalendar CATEGORIES.
 * Seul le nom est stocké dans les éléments ; la couleur vit dans le registre
 * unique de l'application (clé "categories.registry" de settings.json).
 */

#include <windows.h>
#pragma comment(lib, "user32.lib")

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::Categories
{
    /**
     * @brief Description d'une catégorie de classement et de sa couleur associée.
     */
    struct CategoryDefinition
    {
        std::wstring name;       ///< Libellé de la catégorie (ex. "Catégorie rouge", "Travail")
        uint8_t r{ 0 };          ///< Composante rouge de la couleur (0-255)
        uint8_t g{ 0 };          ///< Composante verte de la couleur (0-255)
        uint8_t b{ 0 };          ///< Composante bleue de la couleur (0-255)
        std::wstring shortcut;   ///< Raccourci clavier rapide indicatif (ex. "Ctrl+F2"), vide sinon
        std::vector<std::wstring> accounts; ///< Identifiants des comptes où la catégorie existe (vide : catégorie locale)
    };

    /**
     * @brief Groupe de catégories pour l'affichage « par comptes ».
     */
    struct CategoryGroup
    {
        std::wstring account;          ///< Identifiant du compte, vide pour les catégories locales
        std::vector<size_t> indices;   ///< Positions des catégories dans le registre, dans l'ordre
    };

    /**
     * @brief Replie un texte en minuscules (casse et accents Windows) pour les comparaisons.
     * @param text Texte à replier.
     * @return Copie du texte en minuscules.
     */
    inline std::wstring FoldName(std::wstring_view text)
    {
        std::wstring folded{ text };
        if (!folded.empty())
            ::CharLowerBuffW(folded.data(), static_cast<DWORD>(folded.size()));
        return folded;
    }

    /**
     * @brief Supprime les espaces de début et de fin d'un nom.
     * @param text Texte brut.
     * @return Texte sans espaces périphériques.
     */
    inline std::wstring TrimName(std::wstring_view text)
    {
        size_t first = 0;
        size_t last = text.size();
        while (first < last && (text[first] == L' ' || text[first] == L'\t' || text[first] == L'\r' || text[first] == L'\n'))
            ++first;
        while (last > first && (text[last - 1] == L' ' || text[last - 1] == L'\t' || text[last - 1] == L'\r' || text[last - 1] == L'\n'))
            --last;
        return std::wstring{ text.substr(first, last - first) };
    }

    /**
     * @brief Indique si deux noms de catégories sont identiques (insensible à la casse).
     * @param a Premier nom.
     * @param b Second nom.
     * @return true si les noms sont égaux une fois nettoyés et repliés.
     */
    inline bool NamesEqual(std::wstring_view a, std::wstring_view b)
    {
        return FoldName(TrimName(a)) == FoldName(TrimName(b));
    }

    /**
     * @brief Vérifie qu'un nom est utilisable comme catégorie.
     *
     * Les noms vides ou contenant une virgule sont refusés : la virgule sépare
     * les catégories dans le champ vCard CATEGORIES.
     *
     * @param name Nom proposé.
     * @return true si le nom est valide.
     */
    inline bool IsValidName(std::wstring_view name)
    {
        std::wstring clean = TrimName(name);
        return !clean.empty() && clean.find(L',') == std::wstring::npos && clean.size() <= 100;
    }

    /**
     * @brief Convertit une couleur en texte « #RRGGBB ».
     * @param r Composante rouge.
     * @param g Composante verte.
     * @param b Composante bleue.
     * @return Texte hexadécimal en majuscules.
     */
    inline std::wstring FormatHexColor(uint8_t r, uint8_t g, uint8_t b)
    {
        static const wchar_t digits[] = L"0123456789ABCDEF";
        std::wstring text = L"#";
        for (uint8_t v : { r, g, b })
        {
            text.push_back(digits[v >> 4]);
            text.push_back(digits[v & 0x0F]);
        }
        return text;
    }

    /**
     * @brief Analyse un texte « #RRGGBB » (le dièse est facultatif).
     * @param text Texte à analyser.
     * @param r Composante rouge de sortie.
     * @param g Composante verte de sortie.
     * @param b Composante bleue de sortie.
     * @return true si le texte est une couleur hexadécimale à six chiffres.
     */
    inline bool ParseHexColor(std::wstring_view text, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        std::wstring clean = TrimName(text);
        if (!clean.empty() && clean[0] == L'#')
            clean.erase(0, 1);
        if (clean.size() != 6)
            return false;

        uint32_t value = 0;
        for (wchar_t ch : clean)
        {
            uint32_t digit;
            if (ch >= L'0' && ch <= L'9') digit = static_cast<uint32_t>(ch - L'0');
            else if (ch >= L'a' && ch <= L'f') digit = static_cast<uint32_t>(ch - L'a' + 10);
            else if (ch >= L'A' && ch <= L'F') digit = static_cast<uint32_t>(ch - L'A' + 10);
            else return false;
            value = (value << 4) | digit;
        }
        r = static_cast<uint8_t>(value >> 16);
        g = static_cast<uint8_t>((value >> 8) & 0xFF);
        b = static_cast<uint8_t>(value & 0xFF);
        return true;
    }

    /**
     * @brief Renvoie la liste standard des catégories prédéfinies (palette Fluent).
     * @return Vecteur ordonné ; les six premières portent les raccourcis Ctrl+F2 à Ctrl+F7.
     */
    inline std::vector<CategoryDefinition> DefaultCategories()
    {
        return {
            { L"Catégorie rouge",    232, 17,  35,  L"Ctrl+F2" },
            { L"Catégorie orange",   255, 140, 0,   L"Ctrl+F3" },
            { L"Catégorie jaune",    255, 185, 0,   L"Ctrl+F4" },
            { L"Catégorie verte",    16,  124, 65,  L"Ctrl+F5" },
            { L"Catégorie bleue",    0,   120, 212, L"Ctrl+F6" },
            { L"Catégorie violette", 136, 23,  152, L"Ctrl+F7" },
            { L"Important",          209, 52,  56,  L"" },
            { L"Travail",            0,   99,  177, L"" },
            { L"Personnel",          0,   130, 114, L"" },
            { L"À suivre",           227, 0,   140, L"" }
        };
    }

    /**
     * @brief Couleur de repli stable (calculée d'après le nom) pour une catégorie hors registre.
     * @param name Nom de la catégorie.
     * @param r Composante rouge de sortie.
     * @param g Composante verte de sortie.
     * @param b Composante bleue de sortie.
     */
    inline void FallbackColor(std::wstring_view name, uint8_t& r, uint8_t& g, uint8_t& b)
    {
        static const uint8_t palette[][3] = {
            { 0, 120, 212 }, { 16, 124, 65 }, { 255, 140, 0 }, { 232, 17, 35 },
            { 136, 23, 152 }, { 0, 153, 188 }, { 135, 100, 184 }, { 180, 0, 158 }
        };
        uint32_t hash = 5381;
        for (wchar_t ch : FoldName(TrimName(name)))
            hash = ((hash << 5) + hash) + static_cast<uint32_t>(ch);
        size_t idx = hash % (sizeof(palette) / sizeof(palette[0]));
        r = palette[idx][0];
        g = palette[idx][1];
        b = palette[idx][2];
    }

    /**
     * @brief Registre unique des catégories (nom -> couleur), ordonné.
     *
     * Les noms sont uniques sans tenir compte de la casse. Le registre ne gère
     * que la liste des catégories connues ; l'affectation aux éléments
     * (courriels, contacts, tâches, événements) passe par leurs propres champs.
     */
    class CategoryRegistry
    {
    public:
        /** @brief Construit un registre contenant les catégories standard. */
        CategoryRegistry() : m_items(DefaultCategories()) {}

        /**
         * @brief Construit un registre à partir d'une liste donnée (doublons et noms invalides écartés).
         * @param items Catégories à enregistrer.
         */
        explicit CategoryRegistry(std::vector<CategoryDefinition> const& items)
        {
            for (auto const& item : items)
            {
                if (Add(item.name, item.r, item.g, item.b))
                    m_items.back().accounts = item.accounts;
            }
        }

        /** @brief Liste ordonnée des catégories. */
        std::vector<CategoryDefinition> const& Items() const { return m_items; }

        /** @brief Nombre de catégories. */
        size_t Size() const { return m_items.size(); }

        /**
         * @brief Recherche une catégorie par nom (insensible à la casse).
         * @param name Nom recherché.
         * @return Pointeur vers la définition, nullptr si absente.
         */
        CategoryDefinition const* Find(std::wstring_view name) const
        {
            std::wstring key = FoldName(TrimName(name));
            for (auto const& item : m_items)
                if (FoldName(item.name) == key)
                    return &item;
            return nullptr;
        }

        /**
         * @brief Indique si une catégorie existe.
         * @param name Nom recherché.
         * @return true si le nom est présent dans le registre.
         */
        bool Contains(std::wstring_view name) const { return Find(name) != nullptr; }

        /**
         * @brief Ajoute une catégorie en fin de liste.
         * @param name Nom (valide et inexistant).
         * @param r Composante rouge.
         * @param g Composante verte.
         * @param b Composante bleue.
         * @return true si la catégorie a été ajoutée.
         */
        bool Add(std::wstring_view name, uint8_t r, uint8_t g, uint8_t b)
        {
            if (!IsValidName(name) || Contains(name))
                return false;
            m_items.push_back({ TrimName(name), r, g, b, L"", {} });
            return true;
        }

        /**
         * @brief Rattache une catégorie à un compte (sans doublon).
         * @param name Nom de la catégorie.
         * @param account Identifiant du compte (non vide).
         * @return true si le rattachement est nouveau.
         */
        bool AddAccount(std::wstring_view name, std::wstring_view account)
        {
            auto* item = FindMutable(name);
            if (!item || account.empty())
                return false;
            for (auto const& existing : item->accounts)
                if (existing == account)
                    return false;
            item->accounts.emplace_back(account);
            return true;
        }

        /**
         * @brief Renomme une catégorie en conservant sa couleur et sa position.
         * @param old_name Nom actuel.
         * @param new_name Nouveau nom (valide, non utilisé par une autre catégorie).
         * @return true si le renommage a eu lieu (une simple modification de casse est acceptée).
         */
        bool Rename(std::wstring_view old_name, std::wstring_view new_name)
        {
            auto* current = FindMutable(old_name);
            if (!current || !IsValidName(new_name))
                return false;
            auto* other = FindMutable(new_name);
            if (other && other != current)
                return false;
            current->name = TrimName(new_name);
            return true;
        }

        /**
         * @brief Change la couleur d'une catégorie.
         * @param name Nom de la catégorie.
         * @param r Composante rouge.
         * @param g Composante verte.
         * @param b Composante bleue.
         * @return true si la catégorie existe.
         */
        bool SetColor(std::wstring_view name, uint8_t r, uint8_t g, uint8_t b)
        {
            auto* item = FindMutable(name);
            if (!item)
                return false;
            item->r = r;
            item->g = g;
            item->b = b;
            return true;
        }

        /**
         * @brief Supprime une catégorie du registre (les éléments qui la portent la conservent).
         * @param name Nom de la catégorie.
         * @return true si une catégorie a été supprimée.
         */
        bool Remove(std::wstring_view name)
        {
            std::wstring key = FoldName(TrimName(name));
            auto it = std::find_if(m_items.begin(), m_items.end(),
                [&](CategoryDefinition const& c) { return FoldName(c.name) == key; });
            if (it == m_items.end())
                return false;
            m_items.erase(it);
            return true;
        }

        /**
         * @brief Obtient la couleur d'une catégorie ; une couleur de repli stable est calculée si elle est inconnue.
         * @param name Nom de la catégorie.
         * @param r Composante rouge de sortie.
         * @param g Composante verte de sortie.
         * @param b Composante bleue de sortie.
         * @return true si la catégorie est dans le registre, false si la couleur est un repli.
         */
        bool ColorOf(std::wstring_view name, uint8_t& r, uint8_t& g, uint8_t& b) const
        {
            if (auto const* item = Find(name))
            {
                r = item->r;
                g = item->g;
                b = item->b;
                return true;
            }
            FallbackColor(name, r, g, b);
            return false;
        }

        /**
         * @brief Raccourci clavier indicatif d'une catégorie d'après sa position (Ctrl+F2 à Ctrl+F12).
         * @param index Position dans le registre.
         * @return Texte du raccourci, vide au-delà de la onzième catégorie.
         */
        static std::wstring ShortcutForIndex(size_t index)
        {
            if (index >= 11)
                return L"";
            return L"Ctrl+F" + std::to_wstring(index + 2);
        }

        /**
         * @brief Sérialise le registre en tableau JSON : [{"name":"...","color":"#RRGGBB"}, ...].
         * @return Texte JSON compact.
         */
        std::wstring Serialize() const
        {
            std::wstring json = L"[";
            bool first = true;
            for (auto const& item : m_items)
            {
                if (!first)
                    json += L",";
                first = false;
                json += L"{\"name\":\"" + EscapeJson(item.name) + L"\",\"color\":\"" + FormatHexColor(item.r, item.g, item.b) + L"\"";
                if (!item.accounts.empty())
                {
                    /* identifiants séparés par un saut de ligne (le lecteur ne lit que des chaînes) */
                    std::wstring joined;
                    for (auto const& account : item.accounts)
                        joined += (joined.empty() ? L"" : L"\n") + account;
                    json += L",\"accounts\":\"" + EscapeJson(joined) + L"\"";
                }
                json += L"}";
            }
            json += L"]";
            return json;
        }

        /**
         * @brief Reconstruit un registre depuis un texte JSON produit par Serialize().
         *
         * Tolérant : les entrées illisibles, sans nom valide ou en double sont
         * ignorées, une couleur absente ou invalide devient la couleur de repli.
         * Un texte entièrement illisible donne un registre standard.
         *
         * @param json Texte JSON (tableau d'objets name / color).
         * @return Registre reconstruit.
         */
        static CategoryRegistry Deserialize(std::wstring_view json)
        {
            std::vector<CategoryDefinition> parsed;
            if (!ParseEntries(json, parsed))
                return CategoryRegistry{};
            CategoryRegistry registry{ std::vector<CategoryDefinition>{} };
            for (auto const& entry : parsed)
            {
                if (registry.Add(entry.name, entry.r, entry.g, entry.b))
                    registry.m_items.back().accounts = entry.accounts;
            }
            return registry;
        }

    private:
        CategoryDefinition* FindMutable(std::wstring_view name)
        {
            return const_cast<CategoryDefinition*>(static_cast<CategoryRegistry const*>(this)->Find(name));
        }

        static std::wstring EscapeJson(std::wstring_view text)
        {
            std::wstring out;
            for (wchar_t ch : text)
            {
                if (ch == L'"' || ch == L'\\')
                {
                    out.push_back(L'\\');
                    out.push_back(ch);
                }
                else if (ch < 0x20)
                {
                    static const wchar_t hex[] = L"0123456789abcdef";
                    out += L"\\u00";
                    out.push_back(hex[(ch >> 4) & 0xF]);
                    out.push_back(hex[ch & 0xF]);
                }
                else
                {
                    out.push_back(ch);
                }
            }
            return out;
        }

        /* Lit une chaîne JSON à partir du guillemet ouvrant en position pos ; avance pos après le guillemet fermant. */
        static bool ReadJsonString(std::wstring_view s, size_t& pos, std::wstring& out)
        {
            if (pos >= s.size() || s[pos] != L'"')
                return false;
            ++pos;
            out.clear();
            while (pos < s.size())
            {
                wchar_t ch = s[pos++];
                if (ch == L'"')
                    return true;
                if (ch != L'\\')
                {
                    out.push_back(ch);
                    continue;
                }
                if (pos >= s.size())
                    return false;
                wchar_t esc = s[pos++];
                switch (esc)
                {
                case L'n': out.push_back(L'\n'); break;
                case L'r': out.push_back(L'\r'); break;
                case L't': out.push_back(L'\t'); break;
                case L'u':
                {
                    if (pos + 4 > s.size())
                        return false;
                    uint32_t code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        wchar_t h = s[pos++];
                        uint32_t d;
                        if (h >= L'0' && h <= L'9') d = static_cast<uint32_t>(h - L'0');
                        else if (h >= L'a' && h <= L'f') d = static_cast<uint32_t>(h - L'a' + 10);
                        else if (h >= L'A' && h <= L'F') d = static_cast<uint32_t>(h - L'A' + 10);
                        else return false;
                        code = (code << 4) | d;
                    }
                    out.push_back(static_cast<wchar_t>(code));
                    break;
                }
                default: out.push_back(esc); break;
                }
            }
            return false;
        }

        static void SkipSpaces(std::wstring_view s, size_t& pos)
        {
            while (pos < s.size() && (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\r' || s[pos] == L'\n'))
                ++pos;
        }

        /* Analyse [{"name":"..","color":".."},...] ; false si la structure n'est pas un tableau d'objets. */
        static bool ParseEntries(std::wstring_view s, std::vector<CategoryDefinition>& out)
        {
            size_t pos = 0;
            SkipSpaces(s, pos);
            if (pos >= s.size() || s[pos] != L'[')
                return false;
            ++pos;
            for (;;)
            {
                SkipSpaces(s, pos);
                if (pos >= s.size())
                    return false;
                if (s[pos] == L']')
                    return true;
                if (s[pos] == L',')
                {
                    ++pos;
                    continue;
                }
                if (s[pos] != L'{')
                    return false;
                ++pos;

                CategoryDefinition entry;
                bool has_color = false;
                for (;;)
                {
                    SkipSpaces(s, pos);
                    if (pos >= s.size())
                        return false;
                    if (s[pos] == L'}')
                    {
                        ++pos;
                        break;
                    }
                    if (s[pos] == L',')
                    {
                        ++pos;
                        continue;
                    }
                    std::wstring key, value;
                    if (!ReadJsonString(s, pos, key))
                        return false;
                    SkipSpaces(s, pos);
                    if (pos >= s.size() || s[pos] != L':')
                        return false;
                    ++pos;
                    SkipSpaces(s, pos);
                    if (!ReadJsonString(s, pos, value))
                        return false;
                    if (key == L"name")
                        entry.name = value;
                    else if (key == L"color")
                        has_color = ParseHexColor(value, entry.r, entry.g, entry.b);
                    else if (key == L"accounts")
                    {
                        size_t start = 0;
                        while (start <= value.size())
                        {
                            size_t stop = value.find(L'\n', start);
                            std::wstring part = value.substr(start, stop == std::wstring::npos ? std::wstring::npos : stop - start);
                            if (!part.empty())
                                entry.accounts.push_back(std::move(part));
                            if (stop == std::wstring::npos)
                                break;
                            start = stop + 1;
                        }
                    }
                }
                if (!has_color)
                    FallbackColor(entry.name, entry.r, entry.g, entry.b);
                out.push_back(std::move(entry));
            }
        }

        std::vector<CategoryDefinition> m_items;
    };

    /**
     * @brief Regroupe les catégories par compte pour l'affichage.
     *
     * Le groupe des catégories locales (sans compte connu) vient en premier, puis un
     * groupe par compte de @p known_accounts, dans cet ordre. Une catégorie rattachée à
     * plusieurs comptes figure dans chacun. Les comptes inconnus (supprimés) sont ignorés ;
     * une catégorie qui n'a que des comptes inconnus est locale. Les groupes vides sont omis.
     *
     * @param items Catégories du registre.
     * @param known_accounts Identifiants des comptes existants, dans l'ordre d'affichage.
     * @return Groupes ; vide si aucune catégorie n'est rattachée à un compte connu (affichage à plat).
     */
    inline std::vector<CategoryGroup> GroupByAccount(
        std::vector<CategoryDefinition> const& items,
        std::vector<std::wstring> const& known_accounts)
    {
        CategoryGroup local;
        std::vector<CategoryGroup> per_account;
        for (auto const& account : known_accounts)
            per_account.push_back({ account, {} });

        bool any_account = false;
        for (size_t i = 0; i < items.size(); ++i)
        {
            bool placed = false;
            for (auto& group : per_account)
            {
                if (std::find(items[i].accounts.begin(), items[i].accounts.end(), group.account) != items[i].accounts.end())
                {
                    group.indices.push_back(i);
                    placed = true;
                    any_account = true;
                }
            }
            if (!placed)
                local.indices.push_back(i);
        }
        if (!any_account)
            return {};

        std::vector<CategoryGroup> result;
        if (!local.indices.empty())
            result.push_back(std::move(local));
        for (auto& group : per_account)
            if (!group.indices.empty())
                result.push_back(std::move(group));
        return result;
    }

    /**
     * @brief Découpe une liste de catégories séparées par des virgules (champ vCard CATEGORIES).
     * @param list Texte « Travail, Important ».
     * @return Noms nettoyés, sans vides ni doublons (casse ignorée), dans l'ordre d'apparition.
     */
    inline std::vector<std::wstring> SplitCategories(std::wstring_view list)
    {
        std::vector<std::wstring> names;
        size_t start = 0;
        while (start <= list.size())
        {
            size_t comma = list.find(L',', start);
            std::wstring part = TrimName(list.substr(start, comma == std::wstring_view::npos ? std::wstring_view::npos : comma - start));
            if (!part.empty() && std::none_of(names.begin(), names.end(), [&](std::wstring const& n) { return NamesEqual(n, part); }))
                names.push_back(part);
            if (comma == std::wstring_view::npos)
                break;
            start = comma + 1;
        }
        return names;
    }

    /**
     * @brief Assemble des noms de catégories en texte séparé par des virgules.
     * @param names Noms de catégories.
     * @return Texte « Travail, Important » (vide si aucun nom).
     */
    inline std::wstring JoinCategories(std::vector<std::wstring> const& names)
    {
        std::wstring joined;
        for (auto const& name : names)
        {
            if (!joined.empty())
                joined += L", ";
            joined += name;
        }
        return joined;
    }

    /**
     * @brief Ajoute ou retire une catégorie d'une liste (sans doublon, casse ignorée).
     * @param names Liste à modifier.
     * @param name Catégorie concernée.
     * @param on true pour l'ajouter, false pour la retirer.
     * @return true si la liste a changé.
     */
    inline bool SetCategoryPresent(std::vector<std::wstring>& names, std::wstring_view name, bool on)
    {
        auto it = std::find_if(names.begin(), names.end(), [&](std::wstring const& n) { return NamesEqual(n, name); });
        if (on)
        {
            if (it != names.end() || !IsValidName(name))
                return false;
            names.push_back(TrimName(name));
            return true;
        }
        if (it == names.end())
            return false;
        names.erase(it);
        return true;
    }

    /**
     * @brief Applique un renommage de catégorie à la liste d'un élément (remplace le nom en évitant les doublons).
     * @param names Liste de l'élément.
     * @param old_name Ancien nom.
     * @param new_name Nouveau nom.
     * @return true si la liste a changé.
     */
    inline bool RenameInList(std::vector<std::wstring>& names, std::wstring_view old_name, std::wstring_view new_name)
    {
        auto it = std::find_if(names.begin(), names.end(), [&](std::wstring const& n) { return NamesEqual(n, old_name); });
        if (it == names.end())
            return false;
        std::wstring replacement = TrimName(new_name);
        bool already = std::any_of(names.begin(), names.end(), [&](std::wstring const& n) { return NamesEqual(n, replacement) && !NamesEqual(n, old_name); });
        if (already)
            names.erase(it);
        else
            *it = replacement;
        return true;
    }

    /**
     * @brief Critère de filtrage par catégorie appliqué aux listes (courriels, contacts, tâches, événements).
     *
     * Un élément correspond s'il porte au moins une des catégories choisies
     * (logique « ou »). Avec @c include_uncategorized, les éléments sans
     * aucune catégorie correspondent aussi. Un filtre vide laisse tout passer.
     */
    struct CategoryFilter
    {
        std::vector<std::wstring> selected;   ///< Catégories retenues
        bool include_uncategorized{ false };  ///< Retient aussi les éléments sans catégorie

        /** @brief true si aucun critère n'est actif (tous les éléments passent). */
        bool IsEmpty() const { return selected.empty() && !include_uncategorized; }
    };

    /**
     * @brief Teste si un élément satisfait un filtre par catégorie.
     * @param item_categories Catégories portées par l'élément.
     * @param filter Critère de filtrage.
     * @return true si l'élément doit rester affiché.
     */
    inline bool MatchesCategoryFilter(std::vector<std::wstring> const& item_categories, CategoryFilter const& filter)
    {
        if (filter.IsEmpty())
            return true;
        if (item_categories.empty())
            return filter.include_uncategorized;
        for (auto const& wanted : filter.selected)
            for (auto const& have : item_categories)
                if (NamesEqual(wanted, have))
                    return true;
        return false;
    }

    /**
     * @brief Ajoute ou retire une catégorie du filtre (bascule du menu de filtrage).
     * @param filter Filtre à modifier.
     * @param name Catégorie à basculer.
     */
    inline void ToggleFilterCategory(CategoryFilter& filter, std::wstring_view name)
    {
        if (!SetCategoryPresent(filter.selected, name, true))
            SetCategoryPresent(filter.selected, name, false);
    }
}
