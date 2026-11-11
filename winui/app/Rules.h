#pragma once

/**
 * @file Rules.h
 * @brief Règles de messagerie : modèle, critères, actions, exécution à la réception,
 *        règles rapides, conversion vers les règles serveur (Microsoft Graph messageRules)
 *        et vers un script Sieve (ManageSieve).
 *
 * Logique pure (sans WinRT) pour pouvoir être testée (tests/unit/test_rules.cpp) ;
 * la persistance (RuleStore.h) et l'interface (MainWindow.Rules.cpp) sont à part.
 *
 * Une règle se compose de critères (combinés par « tous » ou « au moins un »),
 * d'actions et d'un drapeau « arrêter le traitement ». Une règle « serveur » n'est jamais
 * exécutée par l'application : elle est envoyée au serveur (Graph ou Sieve).
 */

#include "Categories.h"

#include <cstdint>
#include <cwchar>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace EvolutionWin::Rules
{
    /** @brief Élément du message testé par un critère. */
    enum class Field
    {
        Sender = 0,      ///< Expéditeur (nom ou adresse)
        SenderDomain,    ///< Domaine de l'adresse de l'expéditeur
        Subject,         ///< Objet
        Recipients,      ///< Destinataires (À ou Cc)
        Header,          ///< Un en-tête MIME (nom dans Condition::header)
        HasAttachment    ///< Présence d'une pièce jointe (pas de texte)
    };

    /** @brief Nombre de critères disponibles. */
    inline constexpr int FieldCount = 6;

    /** @brief Comparaison appliquée au texte du critère. */
    enum class Op
    {
        Contains = 0,    ///< contient
        NotContains,     ///< ne contient pas
        Equals,          ///< est égal à
        StartsWith,      ///< commence par
        EndsWith         ///< se termine par
    };

    /** @brief Nombre de comparaisons disponibles. */
    inline constexpr int OpCount = 5;

    /** @brief Type d'action d'une règle. */
    enum class ActionType
    {
        MoveToFolder = 0,  ///< Déplacer dans un dossier (compte + dossier)
        CopyToFolder,      ///< Copier dans un dossier
        MarkRead,          ///< Marquer comme lu
        MarkFlagged,       ///< Poser le drapeau
        AssignCategory,    ///< Affecter une catégorie
        Delete             ///< Supprimer (vers la corbeille)
    };

    /** @brief Nombre de types d'action. */
    inline constexpr int ActionTypeCount = 6;

    /** @brief Un critère d'une règle. */
    struct Condition
    {
        Field field{ Field::Sender };   ///< Élément testé
        Op op{ Op::Contains };          ///< Comparaison
        std::wstring text;              ///< Texte recherché (inutilisé pour HasAttachment)
        std::wstring header;            ///< Nom de l'en-tête (Field::Header)
    };

    /** @brief Une action d'une règle. */
    struct Action
    {
        ActionType type{ ActionType::MoveToFolder };  ///< Type
        std::wstring account;    ///< Compte du dossier de destination (déplacer / copier)
        std::wstring folder;     ///< Chemin complet du dossier de destination
        std::wstring category;   ///< Nom de la catégorie (AssignCategory)
    };

    /** @brief Une règle de messagerie. */
    struct Rule
    {
        std::wstring id;                    ///< Identifiant stable
        std::wstring name;                  ///< Nom affiché
        bool enabled{ true };               ///< Règle active
        bool match_all{ true };             ///< true : tous les critères ; false : au moins un
        bool stop{ false };                 ///< Ne pas appliquer les règles suivantes
        bool server{ false };               ///< Règle exécutée par le serveur (synchronisée), jamais sur ce poste
        std::wstring account;               ///< Compte concerné (vide : tous les comptes)
        int64_t since{ 0 };                 ///< Secondes Unix : à la réception, seuls les messages datés de cet instant ou après sont traités
        std::vector<Condition> conditions;  ///< Critères
        std::vector<Action> actions;        ///< Actions
    };

    /** @brief Données d'un message soumises aux critères (champs inconnus laissés vides). */
    struct MessageData
    {
        std::wstring from;          ///< Champ « De » complet (« Nom <adresse> »)
        std::wstring to;            ///< Champ « À »
        std::wstring cc;            ///< Champ « Cc » (connu seulement si details_known)
        std::wstring subject;       ///< Objet
        bool has_attachment{ false };                    ///< Pièce jointe présente
        bool details_known{ false };                     ///< Cc et en-têtes renseignés
        std::map<std::wstring, std::wstring> headers;    ///< En-têtes (nom replié en minuscules -> valeur)
    };

    /** @brief Résultat du traitement d'un message : ce que les règles décident de faire. */
    struct Outcome
    {
        bool matched{ false };                      ///< Au moins une règle correspond
        bool mark_read{ false };                    ///< Marquer comme lu
        bool mark_flagged{ false };                 ///< Poser le drapeau
        std::vector<std::wstring> categories;       ///< Catégories à affecter
        std::vector<Action> copies;                 ///< Copies à faire (avant tout déplacement)
        bool has_move{ false };                     ///< Déplacement demandé
        Action move;                                ///< Destination du déplacement
        bool remove{ false };                       ///< Suppression demandée (corbeille)
    };

    /* ------------------------------------------------------------------ */
    /*  Extraction d'adresses                                              */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Adresse électronique contenue dans un champ « De » ou « À ».
     * @param field Texte du type « Nom <jean@exemple.fr> » ou « jean@exemple.fr ».
     * @return Adresse (casse d'origine, sans chevrons), vide si aucune.
     */
    inline std::wstring AddressOf(std::wstring_view field)
    {
        size_t at = field.find(L'@');
        if (at == std::wstring_view::npos)
            return L"";
        auto stop = [](wchar_t ch)
        {
            return ch == L'<' || ch == L'>' || ch == L' ' || ch == L'"' || ch == L',' || ch == L';' || ch == L'\t';
        };
        size_t begin = at;
        while (begin > 0 && !stop(field[begin - 1]))
            --begin;
        size_t end = at + 1;
        while (end < field.size() && !stop(field[end]))
            ++end;
        return std::wstring{ field.substr(begin, end - begin) };
    }

    /**
     * @brief Domaine d'une adresse présente dans un champ (partie après « @ », en minuscules).
     * @param field Champ « De » ou « À ».
     * @return Domaine, vide si aucune adresse.
     */
    inline std::wstring DomainOf(std::wstring_view field)
    {
        std::wstring address = AddressOf(field);
        size_t at = address.rfind(L'@');
        if (at == std::wstring::npos)
            return L"";
        return Categories::FoldName(std::wstring_view{ address }.substr(at + 1));
    }

    /* ------------------------------------------------------------------ */
    /*  Évaluation                                                         */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Compare un texte à l'attendu selon l'opérateur (insensible à la casse et aux accents).
     * @param op Comparaison.
     * @param haystack Texte du message.
     * @param needle Texte du critère (déjà replié).
     * @return true si la comparaison est vraie.
     */
    inline bool CompareText(Op op, std::wstring_view haystack, std::wstring_view needle)
    {
        std::wstring hay = Categories::FoldName(haystack);
        switch (op)
        {
        case Op::Contains:
            return hay.find(needle) != std::wstring::npos;
        case Op::NotContains:
            return hay.find(needle) == std::wstring::npos;
        case Op::Equals:
            return hay == needle;
        case Op::StartsWith:
            return hay.size() >= needle.size() && hay.compare(0, needle.size(), needle) == 0;
        case Op::EndsWith:
            return hay.size() >= needle.size() && hay.compare(hay.size() - needle.size(), needle.size(), needle) == 0;
        }
        return false;
    }

    /**
     * @brief Indique si un critère est satisfait par un message.
     * @param condition Critère (un critère de texte vide ne correspond jamais).
     * @param message Données du message.
     * @return true si le critère est satisfait.
     */
    inline bool ConditionMatches(Condition const& condition, MessageData const& message)
    {
        if (condition.field == Field::HasAttachment)
            return message.has_attachment;

        std::wstring needle = Categories::FoldName(Categories::TrimName(condition.text));
        if (needle.empty())
            return false;

        switch (condition.field)
        {
        case Field::Sender:
            return CompareText(condition.op, message.from, needle);
        case Field::SenderDomain:
        {
            while (!needle.empty() && needle.front() == L'@')
                needle.erase(needle.begin());
            std::wstring domain = DomainOf(message.from);
            if (needle.empty() || domain.empty())
                return false;
            if (condition.op == Op::Equals || condition.op == Op::Contains)
            {
                /* « exemple.fr » correspond à exemple.fr et à ses sous-domaines */
                bool sub = domain.size() > needle.size() && domain.compare(domain.size() - needle.size(), needle.size(), needle) == 0
                    && domain[domain.size() - needle.size() - 1] == L'.';
                if (condition.op == Op::Equals)
                    return domain == needle || sub;
                return domain.find(needle) != std::wstring::npos;
            }
            return CompareText(condition.op, domain, needle);
        }
        case Field::Subject:
            return CompareText(condition.op, message.subject, needle);
        case Field::Recipients:
        {
            std::wstring all = message.to;
            if (!message.cc.empty())
                all += L", " + message.cc;
            return CompareText(condition.op, all, needle);
        }
        case Field::Header:
        {
            std::wstring name = Categories::FoldName(Categories::TrimName(condition.header));
            auto it = message.headers.find(name);
            std::wstring value = it == message.headers.end() ? std::wstring{} : it->second;
            /* un en-tête absent : « ne contient pas » est vrai, tout le reste est faux */
            if (it == message.headers.end())
                return condition.op == Op::NotContains;
            return CompareText(condition.op, value, needle);
        }
        case Field::HasAttachment:
            break;
        }
        return false;
    }

    /**
     * @brief Indique si une règle correspond à un message (règle sans critère : jamais).
     * @param rule Règle testée.
     * @param message Données du message.
     * @return true si les critères sont satisfaits selon « tous » / « au moins un ».
     */
    inline bool RuleMatches(Rule const& rule, MessageData const& message)
    {
        if (rule.conditions.empty())
            return false;
        for (auto const& condition : rule.conditions)
        {
            bool hit = ConditionMatches(condition, message);
            if (rule.match_all && !hit)
                return false;
            if (!rule.match_all && hit)
                return true;
        }
        return rule.match_all;
    }

    /**
     * @brief Indique si l'évaluation d'une règle exige le Cc ou les en-têtes du message
     *        (absents de la liste des messages : il faut alors lire le message).
     * @param rule Règle testée.
     * @return true si un critère porte sur les destinataires ou un en-tête.
     */
    inline bool NeedsDetails(Rule const& rule)
    {
        for (auto const& condition : rule.conditions)
            if (condition.field == Field::Recipients || condition.field == Field::Header)
                return true;
        return false;
    }

    /**
     * @brief Indique si une règle s'applique à un compte donné.
     * @param rule Règle.
     * @param account Compte du message.
     * @return true si la règle vise tous les comptes ou celui-ci.
     */
    inline bool AppliesToAccount(Rule const& rule, std::wstring_view account)
    {
        return rule.account.empty() || rule.account == account;
    }

    /**
     * @brief Calcule ce que les règles locales décident pour un message.
     *
     * Les règles sont parcourues dans l'ordre ; les règles désactivées, serveur ou d'un autre
     * compte sont ignorées. Un déplacement ou une suppression met fin au traitement (le message
     * a quitté le dossier), tout comme une règle marquée « arrêter ». Les copies, indicateurs et
     * catégories s'accumulent. Si plusieurs règles demandent un déplacement, la première l'emporte.
     *
     * @param rules Règles par ordre de priorité.
     * @param message Données du message.
     * @param account Compte du message.
     * @param message_date Date du message (secondes Unix, 0 si inconnue) : comparée à Rule::since
     *        quand @p respect_since est vrai.
     * @param respect_since true à la réception (ignore les règles créées après le message), false pour « Exécuter maintenant ».
     * @return Décision cumulée.
     */
    inline Outcome Evaluate(std::vector<Rule> const& rules, MessageData const& message, std::wstring_view account,
                            int64_t message_date, bool respect_since)
    {
        Outcome outcome;
        for (auto const& rule : rules)
        {
            if (!rule.enabled || rule.server || !AppliesToAccount(rule, account))
                continue;
            if (respect_since && rule.since > 0 && message_date > 0 && message_date < rule.since)
                continue;
            if (!RuleMatches(rule, message))
                continue;

            outcome.matched = true;
            bool leaves = false;
            for (auto const& action : rule.actions)
            {
                switch (action.type)
                {
                case ActionType::MoveToFolder:
                    if (!outcome.has_move && !outcome.remove && !action.folder.empty())
                    {
                        outcome.has_move = true;
                        outcome.move = action;
                    }
                    leaves = leaves || !action.folder.empty();
                    break;
                case ActionType::CopyToFolder:
                    if (!action.folder.empty())
                        outcome.copies.push_back(action);
                    break;
                case ActionType::MarkRead:
                    outcome.mark_read = true;
                    break;
                case ActionType::MarkFlagged:
                    outcome.mark_flagged = true;
                    break;
                case ActionType::AssignCategory:
                {
                    std::wstring name = Categories::TrimName(action.category);
                    bool known = false;
                    for (auto const& existing : outcome.categories)
                        known = known || Categories::NamesEqual(existing, name);
                    if (!name.empty() && !known)
                        outcome.categories.push_back(std::move(name));
                    break;
                }
                case ActionType::Delete:
                    if (!outcome.has_move)
                        outcome.remove = true;
                    leaves = true;
                    break;
                }
            }
            if (leaves || rule.stop)
                break;
        }
        return outcome;
    }

    /* ------------------------------------------------------------------ */
    /*  Règles rapides                                                     */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Objet d'un message débarrassé de ses préfixes de réponse / transfert (« Re: », « TR : », « Fwd: »…).
     * @param subject Objet d'origine.
     * @return Objet nettoyé (espaces périphériques retirés).
     */
    inline std::wstring CleanSubject(std::wstring_view subject)
    {
        std::wstring text = Categories::TrimName(subject);
        bool again = true;
        while (again)
        {
            again = false;
            for (auto const* prefix : { L"re", L"tr", L"fw", L"fwd", L"rv", L"aw", L"wg", L"r" })
            {
                size_t length = std::wstring_view{ prefix }.size();
                if (text.size() > length + 1 && Categories::FoldName(std::wstring_view{ text }.substr(0, length)) == prefix)
                {
                    size_t pos = length;
                    while (pos < text.size() && text[pos] == L' ')
                        ++pos;
                    if (pos < text.size() && text[pos] == L':')
                    {
                        text = Categories::TrimName(std::wstring_view{ text }.substr(pos + 1));
                        again = true;
                        break;
                    }
                }
            }
        }
        return text;
    }

    /**
     * @brief Règle rapide préremplie à partir d'un message (clic droit > Créer une règle).
     *
     * Critères : l'expéditeur (adresse) et l'objet nettoyé ; l'éditeur rapide permet de n'en garder qu'un.
     * Aucune action n'est posée : l'utilisateur la choisit.
     *
     * @param from Champ « De » du message.
     * @param subject Objet du message.
     * @param account Compte du message (la règle ne vise que lui).
     * @return Règle sans identifiant, avec un nom proposé.
     */
    inline Rule MakeQuickRule(std::wstring_view from, std::wstring_view subject, std::wstring_view account)
    {
        Rule rule;
        rule.account = std::wstring{ account };
        std::wstring address = AddressOf(from);
        if (!address.empty())
        {
            Condition sender;
            sender.field = Field::Sender;
            sender.text = address;
            rule.conditions.push_back(sender);
            rule.name = L"Messages de " + address;
        }
        std::wstring cleaned = CleanSubject(subject);
        if (!cleaned.empty())
        {
            Condition by_subject;
            by_subject.field = Field::Subject;
            by_subject.text = cleaned;
            rule.conditions.push_back(by_subject);
            if (rule.name.empty())
                rule.name = L"Objet : " + cleaned;
        }
        return rule;
    }

    /* ------------------------------------------------------------------ */
    /*  Sérialisation (réglages)                                           */
    /* ------------------------------------------------------------------ */

    namespace Detail
    {
        /**
         * @brief Protège un champ : « \ » -> « \\ », tabulation -> « \t », saut de ligne -> « \n », retour chariot -> « \r ».
         * @param text Texte brut.
         * @return Texte sans tabulation ni saut de ligne.
         */
        inline std::wstring Escape(std::wstring_view text)
        {
            std::wstring out;
            for (wchar_t ch : text)
            {
                switch (ch)
                {
                case L'\\': out += L"\\\\"; break;
                case L'\t': out += L"\\t"; break;
                case L'\n': out += L"\\n"; break;
                case L'\r': out += L"\\r"; break;
                default: out += ch; break;
                }
            }
            return out;
        }

        /**
         * @brief Opération inverse de Escape().
         * @param text Champ protégé.
         * @return Texte d'origine.
         */
        inline std::wstring Unescape(std::wstring_view text)
        {
            std::wstring out;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == L'\\' && i + 1 < text.size())
                {
                    ++i;
                    out += text[i] == L't' ? L'\t' : text[i] == L'n' ? L'\n' : text[i] == L'r' ? L'\r' : text[i];
                }
                else
                    out += text[i];
            }
            return out;
        }

        /**
         * @brief Découpe une ligne selon les tabulations.
         * @param line Ligne sans saut de ligne.
         * @return Champs (non décodés).
         */
        inline std::vector<std::wstring_view> SplitTabs(std::wstring_view line)
        {
            std::vector<std::wstring_view> parts;
            size_t start = 0;
            while (true)
            {
                size_t tab = line.find(L'\t', start);
                if (tab == std::wstring_view::npos)
                {
                    parts.push_back(line.substr(start));
                    break;
                }
                parts.push_back(line.substr(start, tab - start));
                start = tab + 1;
            }
            return parts;
        }

        /**
         * @brief Lit un entier décimal.
         * @param text Texte.
         * @param fallback Valeur si illisible.
         * @return Entier lu.
         */
        inline int64_t ToInt(std::wstring_view text, int64_t fallback)
        {
            if (text.empty())
                return fallback;
            bool negative = text[0] == L'-';
            int64_t value = 0;
            for (size_t i = negative ? 1 : 0; i < text.size(); ++i)
            {
                if (text[i] < L'0' || text[i] > L'9')
                    return fallback;
                value = value * 10 + (text[i] - L'0');
            }
            return negative ? -value : value;
        }
    }

    /**
     * @brief Sérialise les règles : une ligne par élément, champs séparés par des tabulations.
     *
     *   R id actif tous arreter serveur since compte nom
     *   C champ operateur texte entete        (critère de la règle précédente)
     *   A type compte dossier categorie       (action de la règle précédente)
     *
     * @param rules Règles à écrire.
     * @return Texte à stocker dans les réglages.
     */
    inline std::wstring Serialize(std::vector<Rule> const& rules)
    {
        using Detail::Escape;
        std::wstring out;
        for (auto const& rule : rules)
        {
            out += L"R\t" + Escape(rule.id) + L"\t" + (rule.enabled ? L"1" : L"0") + L"\t" + (rule.match_all ? L"1" : L"0")
                + L"\t" + (rule.stop ? L"1" : L"0") + L"\t" + (rule.server ? L"1" : L"0") + L"\t" + std::to_wstring(rule.since)
                + L"\t" + Escape(rule.account) + L"\t" + Escape(rule.name) + L"\n";
            for (auto const& c : rule.conditions)
                out += L"C\t" + std::to_wstring(static_cast<int>(c.field)) + L"\t" + std::to_wstring(static_cast<int>(c.op))
                    + L"\t" + Escape(c.text) + L"\t" + Escape(c.header) + L"\n";
            for (auto const& a : rule.actions)
                out += L"A\t" + std::to_wstring(static_cast<int>(a.type)) + L"\t" + Escape(a.account) + L"\t" + Escape(a.folder)
                    + L"\t" + Escape(a.category) + L"\n";
        }
        return out;
    }

    /**
     * @brief Relit des règles écrites par Serialize() ; les lignes illisibles sont ignorées.
     * @param text Texte des réglages.
     * @return Règles lues, dans l'ordre.
     */
    inline std::vector<Rule> Deserialize(std::wstring_view text)
    {
        using Detail::Unescape;
        using Detail::ToInt;
        std::vector<Rule> rules;
        size_t start = 0;
        while (start < text.size())
        {
            size_t stop = text.find(L'\n', start);
            std::wstring_view line = text.substr(start, stop == std::wstring_view::npos ? std::wstring_view::npos : stop - start);
            start = stop == std::wstring_view::npos ? text.size() : stop + 1;

            auto parts = Detail::SplitTabs(line);
            if (parts.empty() || parts[0].size() != 1)
                continue;

            if (parts[0] == L"R" && parts.size() >= 9)
            {
                Rule rule;
                rule.id = Unescape(parts[1]);
                rule.enabled = parts[2] == L"1";
                rule.match_all = parts[3] == L"1";
                rule.stop = parts[4] == L"1";
                rule.server = parts[5] == L"1";
                rule.since = ToInt(parts[6], 0);
                rule.account = Unescape(parts[7]);
                rule.name = Unescape(parts[8]);
                rules.push_back(std::move(rule));
            }
            else if (parts[0] == L"C" && parts.size() >= 5 && !rules.empty())
            {
                int64_t field = ToInt(parts[1], -1);
                int64_t op = ToInt(parts[2], -1);
                if (field < 0 || field >= FieldCount || op < 0 || op >= OpCount)
                    continue;
                Condition c;
                c.field = static_cast<Field>(field);
                c.op = static_cast<Op>(op);
                c.text = Unescape(parts[3]);
                c.header = Unescape(parts[4]);
                rules.back().conditions.push_back(std::move(c));
            }
            else if (parts[0] == L"A" && parts.size() >= 5 && !rules.empty())
            {
                int64_t type = ToInt(parts[1], -1);
                if (type < 0 || type >= ActionTypeCount)
                    continue;
                Action a;
                a.type = static_cast<ActionType>(type);
                a.account = Unescape(parts[2]);
                a.folder = Unescape(parts[3]);
                a.category = Unescape(parts[4]);
                rules.back().actions.push_back(std::move(a));
            }
        }
        return rules;
    }

    /* ------------------------------------------------------------------ */
    /*  Messages déjà traités à la réception                               */
    /* ------------------------------------------------------------------ */

    /**
     * @brief Mémoire des messages déjà passés dans les règles (évite de réappliquer une règle
     *        à un message que l'utilisateur a remis dans la boîte de réception).
     *
     * Les entrées les plus anciennes sont oubliées au-delà de la capacité.
     */
    class ProcessedSet
    {
    public:
        /** @brief Nombre maximal d'entrées mémorisées. */
        static constexpr size_t Capacity = 3000;

        /**
         * @brief Clé d'un message.
         * @param account Compte.
         * @param folder Dossier.
         * @param uid Identifiant du message.
         * @return Clé unique.
         */
        static std::wstring Key(std::wstring_view account, std::wstring_view folder, std::wstring_view uid)
        {
            std::wstring key{ account };
            key += L'\x1f';
            key += folder;
            key += L'\x1f';
            key += uid;
            return key;
        }

        /** @brief Indique si la clé est connue. */
        bool Contains(std::wstring const& key) const { return m_set.count(key) != 0; }

        /**
         * @brief Mémorise une clé.
         * @param key Clé à ajouter.
         * @return true si elle était nouvelle.
         */
        bool Add(std::wstring const& key)
        {
            if (!m_set.insert(key).second)
                return false;
            m_order.push_back(key);
            while (m_order.size() > Capacity)
            {
                m_set.erase(m_order.front());
                m_order.erase(m_order.begin());
            }
            return true;
        }

        /** @brief Nombre d'entrées. */
        size_t Size() const { return m_order.size(); }

        /** @brief Texte à stocker (une clé par ligne). */
        std::wstring Serialize() const
        {
            std::wstring out;
            for (auto const& key : m_order)
                out += key + L"\n";
            return out;
        }

        /**
         * @brief Relit un texte écrit par Serialize().
         * @param text Texte stocké.
         * @return Ensemble reconstitué.
         */
        static ProcessedSet Deserialize(std::wstring_view text)
        {
            ProcessedSet set;
            size_t start = 0;
            while (start < text.size())
            {
                size_t stop = text.find(L'\n', start);
                std::wstring_view line = text.substr(start, stop == std::wstring_view::npos ? std::wstring_view::npos : stop - start);
                start = stop == std::wstring_view::npos ? text.size() : stop + 1;
                if (!line.empty())
                    set.Add(std::wstring{ line });
            }
            return set;
        }

    private:
        std::set<std::wstring> m_set;
        std::vector<std::wstring> m_order;
    };

    /* ------------------------------------------------------------------ */
    /*  Règles serveur : Microsoft Graph (messageRules)                    */
    /* ------------------------------------------------------------------ */

    /** @brief Préfixe du nom des règles créées sur le serveur par Evolution (permet de les retrouver et de les remplacer). */
    inline constexpr wchar_t ServerRulePrefix[] = L"[Evolution] ";

    /**
     * @brief Chaîne JSON entre guillemets (échappement de « " », « \ » et des caractères de contrôle).
     * @param text Texte brut.
     * @return Littéral JSON.
     */
    inline std::wstring JsonString(std::wstring_view text)
    {
        std::wstring out = L"\"";
        for (wchar_t ch : text)
        {
            switch (ch)
            {
            case L'"': out += L"\\\""; break;
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (ch < 0x20)
                {
                    wchar_t buffer[8];
                    swprintf_s(buffer, L"\\u%04x", static_cast<unsigned>(ch));
                    out += buffer;
                }
                else
                    out += ch;
                break;
            }
        }
        out += L'"';
        return out;
    }

    /** @brief Résultat de la conversion d'une règle vers Graph. */
    struct GraphConversion
    {
        std::vector<std::wstring> rules;   ///< Objets JSON messageRule (plusieurs si « au moins un » avec plusieurs critères)
        std::wstring reason;               ///< Raison, si la règle n'est pas convertible (rules est alors vide)
    };

    namespace Detail
    {
        /** @brief Clé Graph d'un critère de texte (conditions / exceptions), vide si non pris en charge. */
        inline std::wstring GraphKey(Condition const& c)
        {
            switch (c.field)
            {
            case Field::Sender: return L"senderContains";
            case Field::SenderDomain: return L"senderContains";
            case Field::Subject: return L"subjectContains";
            case Field::Recipients: return L"recipientContains";
            case Field::Header: return L"headerContains";
            case Field::HasAttachment: return L"hasAttachments";
            }
            return L"";
        }

        /** @brief Valeur Graph du texte d'un critère (« @domaine » pour un domaine, « Nom: valeur » pour un en-tête). */
        inline std::wstring GraphText(Condition const& c)
        {
            std::wstring text = Categories::TrimName(c.text);
            if (c.field == Field::SenderDomain)
            {
                while (!text.empty() && text.front() == L'@')
                    text.erase(text.begin());
                return L"@" + text;
            }
            if (c.field == Field::Header)
                return Categories::TrimName(c.header) + L": " + text;
            return text;
        }

        /**
         * @brief Objet JSON des critères d'une liste de conditions (même clé : tableau de chaînes ; hasAttachments : booléen).
         * @param list Critères à convertir (une seule clé répétée est refusée par l'appelant).
         * @return Objet JSON.
         */
        inline std::wstring GraphPredicates(std::vector<Condition const*> const& list)
        {
            std::map<std::wstring, std::vector<std::wstring>> by_key;
            bool attachments = false;
            for (auto const* c : list)
            {
                if (c->field == Field::HasAttachment)
                    attachments = true;
                else if (c->field == Field::Sender && c->op == Op::Equals)
                    by_key[L"fromAddresses"].push_back(JsonString(Categories::TrimName(c->text)));
                else
                    by_key[GraphKey(*c)].push_back(JsonString(GraphText(*c)));
            }
            std::wstring out = L"{";
            bool first = true;
            for (auto const& [key, values] : by_key)
            {
                if (!first)
                    out += L",";
                first = false;
                out += JsonString(key) + L":[";
                for (size_t i = 0; i < values.size(); ++i)
                {
                    if (i)
                        out += L",";
                    out += key == L"fromAddresses" ? L"{\"emailAddress\":{\"address\":" + values[i] + L"}}" : values[i];
                }
                out += L"]";
            }
            if (attachments)
            {
                if (!first)
                    out += L",";
                out += L"\"hasAttachments\":true";
            }
            return out + L"}";
        }
    }

    /**
     * @brief Convertit une règle en règle(s) Microsoft Graph (messageRule), prête(s) pour le moteur.
     *
     * Les dossiers de destination sont écrits sous la forme d'un chemin Camel (« INBOX/Factures ») que le
     * moteur remplace par l'identifiant du dossier Graph. Limites de Graph, qui rendent une règle non
     * convertible (@p reason renseignée) : action « Drapeau », dossier d'un autre compte, critère
     * « commence / finit par », deux critères « tous » portant sur le même élément, critère « ne contient pas »
     * avec « au moins un », règle sans critère ni action. Avec « au moins un », chaque critère donne sa
     * propre règle serveur (Graph combine les critères par « et »).
     *
     * @param rule Règle à convertir (le compte de destination des dossiers doit être celui de la règle).
     * @param sequence_start Numéro de séquence de la première règle produite.
     * @return Règles JSON, ou la raison du refus.
     */
    inline GraphConversion ToGraphRules(Rule const& rule, int sequence_start)
    {
        GraphConversion result;
        if (rule.conditions.empty() || rule.actions.empty())
        {
            result.reason = L"règle sans critère ou sans action";
            return result;
        }

        for (auto const& c : rule.conditions)
        {
            if (c.op == Op::StartsWith || c.op == Op::EndsWith)
            {
                result.reason = L"les critères « commence par » et « se termine par » ne sont pas pris en charge par Exchange Online";
                return result;
            }
            if (c.op == Op::NotContains && c.field == Field::HasAttachment)
            {
                result.reason = L"critère de pièce jointe non convertible";
                return result;
            }
            if (c.op == Op::Equals && c.field != Field::Sender && c.field != Field::SenderDomain)
            {
                result.reason = L"le critère « est égal à » n'est pris en charge par Exchange Online que pour l'expéditeur";
                return result;
            }
        }

        /* actions : partagées par toutes les règles produites */
        std::wstring actions = L"{";
        bool first = true;
        auto add = [&](std::wstring const& member)
        {
            actions += (first ? L"" : L",") + member;
            first = false;
        };
        std::vector<std::wstring> categories;
        for (auto const& a : rule.actions)
        {
            switch (a.type)
            {
            case ActionType::MoveToFolder:
            case ActionType::CopyToFolder:
                if (a.folder.empty() || (!a.account.empty() && !rule.account.empty() && a.account != rule.account))
                {
                    result.reason = L"le dossier de destination doit appartenir au compte de la règle";
                    return result;
                }
                add(std::wstring{ a.type == ActionType::MoveToFolder ? L"\"moveToFolder\":" : L"\"copyToFolder\":" } + JsonString(a.folder));
                break;
            case ActionType::MarkRead:
                add(L"\"markAsRead\":true");
                break;
            case ActionType::MarkFlagged:
                result.reason = L"l'action « Poser le drapeau » n'existe pas dans les règles Exchange Online";
                return result;
            case ActionType::AssignCategory:
                if (!Categories::TrimName(a.category).empty())
                    categories.push_back(JsonString(Categories::TrimName(a.category)));
                break;
            case ActionType::Delete:
                add(L"\"delete\":true");
                break;
            }
        }
        if (!categories.empty())
        {
            std::wstring list = L"\"assignCategories\":[";
            for (size_t i = 0; i < categories.size(); ++i)
                list += (i ? L"," : L"") + categories[i];
            add(list + L"]");
        }
        if (rule.stop)
            add(L"\"stopProcessingRules\":true");
        actions += L"}";

        /* groupes de critères : un seul (« tous ») ou un par critère (« au moins un ») */
        std::vector<std::vector<Condition const*>> groups;
        std::vector<Condition const*> exceptions;
        if (rule.match_all)
        {
            std::vector<Condition const*> positives;
            std::set<std::wstring> keys;
            for (auto const& c : rule.conditions)
            {
                if (c.op == Op::NotContains)
                {
                    exceptions.push_back(&c);
                    continue;
                }
                std::wstring key = c.field == Field::Sender && c.op == Op::Equals ? L"fromAddresses" : Detail::GraphKey(c);
                if (c.field != Field::HasAttachment && !keys.insert(key).second)
                {
                    result.reason = L"deux critères « tous » sur le même élément ne sont pas convertibles pour Exchange Online";
                    return result;
                }
                positives.push_back(&c);
            }
            if (positives.empty())
            {
                result.reason = L"une règle Exchange Online doit comporter au moins un critère positif";
                return result;
            }
            groups.push_back(std::move(positives));
        }
        else
        {
            for (auto const& c : rule.conditions)
            {
                if (c.op == Op::NotContains)
                {
                    result.reason = L"« ne contient pas » ne peut pas se combiner avec « au moins un critère » pour Exchange Online";
                    return result;
                }
                groups.push_back({ &c });
            }
        }

        for (size_t i = 0; i < groups.size(); ++i)
        {
            std::wstring name = rule.name.empty() ? std::wstring{ L"Règle" } : rule.name;
            if (groups.size() > 1)
                name += L" (" + std::to_wstring(i + 1) + L"/" + std::to_wstring(groups.size()) + L")";
            std::wstring json = L"{\"displayName\":" + JsonString(std::wstring{ ServerRulePrefix } + name)
                + L",\"sequence\":" + std::to_wstring(sequence_start + static_cast<int>(i))
                + L",\"isEnabled\":" + (rule.enabled ? L"true" : L"false")
                + L",\"conditions\":" + Detail::GraphPredicates(groups[i])
                + L",\"actions\":" + actions;
            if (!exceptions.empty())
                json += L",\"exceptions\":" + Detail::GraphPredicates(exceptions);
            json += L"}";
            result.rules.push_back(std::move(json));
        }
        return result;
    }

    /** @brief Règles d'un compte prêtes à être envoyées à Exchange Online, avec celles qui ne le sont pas. */
    struct GraphPlan
    {
        std::vector<std::wstring> rules;       ///< Objets JSON messageRule
        std::vector<std::wstring> skipped;     ///< « nom : raison » des règles ignorées
    };

    /**
     * @brief Prépare l'envoi des règles « serveur » d'un compte vers Microsoft Graph.
     * @param rules Toutes les règles.
     * @param account Compte visé.
     * @return Règles convertibles (séquences consécutives) et liste des règles ignorées avec leur raison.
     */
    inline GraphPlan PlanGraphSync(std::vector<Rule> const& rules, std::wstring_view account)
    {
        GraphPlan plan;
        int sequence = 1;
        for (auto const& rule : rules)
        {
            if (!rule.server || !rule.enabled || rule.account != account)
                continue;
            auto converted = ToGraphRules(rule, sequence);
            if (converted.rules.empty())
            {
                plan.skipped.push_back(rule.name + L" : " + converted.reason);
                continue;
            }
            for (auto& json : converted.rules)
                plan.rules.push_back(std::move(json));
            sequence += static_cast<int>(converted.rules.size());
        }
        return plan;
    }

    /**
     * @brief Tableau JSON des règles Graph, tel que le moteur l'attend.
     * @param plan Plan de synchronisation.
     * @return Texte « [ {...}, {...} ] ».
     */
    inline std::wstring GraphRulesArray(GraphPlan const& plan)
    {
        std::wstring out = L"[";
        for (size_t i = 0; i < plan.rules.size(); ++i)
            out += (i ? L"," : L"") + plan.rules[i];
        return out + L"]";
    }

    /* ------------------------------------------------------------------ */
    /*  Règles serveur : Sieve (ManageSieve)                               */
    /* ------------------------------------------------------------------ */

    /** @brief Nom du script envoyé au serveur ManageSieve. */
    inline constexpr wchar_t SieveScriptName[] = L"evolution";

    /**
     * @brief Chaîne Sieve entre guillemets (échappement de « " » et « \ »).
     * @param text Texte brut.
     * @return Littéral Sieve.
     */
    inline std::wstring SieveString(std::wstring_view text)
    {
        std::wstring out = L"\"";
        for (wchar_t ch : text)
        {
            if (ch == L'"' || ch == L'\\')
                out += L'\\';
            if (ch == L'\r' || ch == L'\n')
                ch = L' ';
            out += ch;
        }
        out += L'"';
        return out;
    }

    /** @brief Résultat de la génération d'un script Sieve. */
    struct SieveScript
    {
        std::wstring text;                    ///< Script complet (vide si aucune règle convertible)
        int rule_count{ 0 };                  ///< Règles incluses
        std::vector<std::wstring> skipped;    ///< « nom : raison » des règles ignorées
    };

    namespace Detail
    {
        /** @brief Test Sieve d'un critère (sans le « not » éventuel). */
        inline std::wstring SieveTest(Condition const& c)
        {
            std::wstring text = SieveString(Categories::TrimName(c.text));
            auto match = [&](std::wstring const& header_list, bool address_domain)
            {
                std::wstring tag;
                std::wstring value = text;
                switch (c.op)
                {
                case Op::Equals: tag = L":is"; break;
                case Op::StartsWith:
                    tag = L":matches";
                    value = SieveString(Categories::TrimName(c.text) + L"*");
                    break;
                case Op::EndsWith:
                    tag = L":matches";
                    value = SieveString(L"*" + Categories::TrimName(c.text));
                    break;
                default: tag = L":contains"; break;
                }
                return std::wstring{ address_domain ? L"address :domain " : L"header " } + tag + L" " + header_list + L" " + value;
            };
            switch (c.field)
            {
            case Field::Sender: return match(L"\"from\"", false);
            case Field::SenderDomain:
            {
                std::wstring domain = Categories::TrimName(c.text);
                while (!domain.empty() && domain.front() == L'@')
                    domain.erase(domain.begin());
                return L"address :domain :is \"from\" " + SieveString(domain);
            }
            case Field::Subject: return match(L"\"subject\"", false);
            case Field::Recipients: return match(L"[\"to\", \"cc\"]", false);
            case Field::Header: return match(SieveString(Categories::TrimName(c.header)), false);
            case Field::HasAttachment: return L"header :contains \"content-type\" \"multipart/mixed\"";
            }
            return L"false";
        }
    }

    /**
     * @brief Génère le script Sieve des règles « serveur » d'un compte.
     *
     * Les indicateurs (lu, drapeau, catégories) précèdent le « fileinto » pour s'appliquer à la copie classée ;
     * la suppression utilise le dossier « Trash » (à adapter au serveur). « Copier » exige l'extension « copy ».
     * Règles ignorées : sans critère ni action, ou avec une destination dans un autre compte.
     *
     * @param rules Toutes les règles.
     * @param account Compte visé.
     * @return Script, nombre de règles incluses et règles ignorées.
     */
    inline SieveScript ToSieve(std::vector<Rule> const& rules, std::wstring_view account)
    {
        SieveScript script;
        std::set<std::wstring> requires_;
        std::wstring body;

        for (auto const& rule : rules)
        {
            if (!rule.server || !rule.enabled || rule.account != account)
                continue;
            if (rule.conditions.empty() || rule.actions.empty())
            {
                script.skipped.push_back(rule.name + L" : règle sans critère ou sans action");
                continue;
            }
            bool foreign = false;
            for (auto const& a : rule.actions)
                if ((a.type == ActionType::MoveToFolder || a.type == ActionType::CopyToFolder) && !a.account.empty() && a.account != rule.account)
                    foreign = true;
            if (foreign)
            {
                script.skipped.push_back(rule.name + L" : le dossier de destination doit appartenir au compte de la règle");
                continue;
            }

            std::wstring title = rule.name.empty() ? std::wstring{ L"Règle" } : rule.name;
            for (auto& ch : title)
                if (ch == L'\r' || ch == L'\n')
                    ch = L' ';
            std::wstring block = L"# " + title + L"\n";

            std::wstring tests;
            for (size_t i = 0; i < rule.conditions.size(); ++i)
            {
                auto const& c = rule.conditions[i];
                std::wstring test = Detail::SieveTest(c);
                if (c.op == Op::NotContains)
                    test = L"not " + test;
                tests += (i ? L",\n        " : L"") + test;
            }
            if (rule.conditions.size() == 1)
                block += L"if " + tests + L" {\n";
            else
                block += std::wstring{ rule.match_all ? L"if allof (" : L"if anyof (" } + tests + L") {\n";

            std::wstring flags;
            std::wstring tail;
            bool leaves = false;
            for (auto const& a : rule.actions)
            {
                switch (a.type)
                {
                case ActionType::MarkRead:
                    requires_.insert(L"imap4flags");
                    flags += L"    addflag \"\\\\Seen\";\n";
                    break;
                case ActionType::MarkFlagged:
                    requires_.insert(L"imap4flags");
                    flags += L"    addflag \"\\\\Flagged\";\n";
                    break;
                case ActionType::AssignCategory:
                    if (!Categories::TrimName(a.category).empty())
                    {
                        requires_.insert(L"imap4flags");
                        flags += L"    addflag " + SieveString(Categories::TrimName(a.category)) + L";\n";
                    }
                    break;
                case ActionType::CopyToFolder:
                    requires_.insert(L"fileinto");
                    requires_.insert(L"copy");
                    tail += L"    fileinto :copy " + SieveString(a.folder) + L";\n";
                    break;
                case ActionType::MoveToFolder:
                    requires_.insert(L"fileinto");
                    tail += L"    fileinto " + SieveString(a.folder) + L";\n";
                    leaves = true;
                    break;
                case ActionType::Delete:
                    requires_.insert(L"fileinto");
                    tail += L"    fileinto \"Trash\";\n";
                    leaves = true;
                    break;
                }
            }
            block += flags + tail;
            if (rule.stop || leaves)
                block += L"    stop;\n";
            block += L"}\n\n";

            body += block;
            ++script.rule_count;
        }

        if (script.rule_count == 0)
            return script;

        std::wstring header = L"# Script généré par Evolution (Windows) : ne pas modifier à la main.\nrequire [";
        bool first = true;
        for (auto const& name : requires_)
        {
            header += (first ? L"" : L", ") + SieveString(name);
            first = false;
        }
        header += L"];\n\n";
        script.text = header + body;
        return script;
    }
}
